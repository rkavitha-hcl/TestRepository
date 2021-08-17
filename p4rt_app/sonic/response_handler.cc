/*
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "p4rt_app/sonic/response_handler.h"

#include <vector>

#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "swss/status_code_util.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::google::rpc::Code;

// Converts swss return codes to P4RT GoogleRpcCode.
Code SwssToP4RTErrorCode(const std::string& status_str) {
  const swss::StatusCode status_code = swss::strToStatusCode(status_str);
  std::map<swss::StatusCode, Code> status_map = {
      {swss::StatusCode::SWSS_RC_SUCCESS, Code::OK},
      {swss::StatusCode::SWSS_RC_INVALID_PARAM, Code::INVALID_ARGUMENT},
      {swss::StatusCode::SWSS_RC_DEADLINE_EXCEEDED, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_UNAVAIL, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_NOT_FOUND, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_NO_MEMORY, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_EXISTS, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_PERMISSION_DENIED, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_FULL, Code::RESOURCE_EXHAUSTED},
      {swss::StatusCode::SWSS_RC_IN_USE, Code::INVALID_ARGUMENT},
      {swss::StatusCode::SWSS_RC_INTERNAL, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_UNKNOWN, Code::INTERNAL},
      {swss::StatusCode::SWSS_RC_UNIMPLEMENTED, Code::UNIMPLEMENTED}};
  auto it = status_map.find(status_code);
  // TODO (kishanps) raise critical error for all INTERNAL errors.
  if (it != status_map.end()) {
    return it->second;
  } else {
    return Code::UNKNOWN;
  }
}

// Get expected responses from the notification channel.
// It is required to get all the expected responses first and then lookup for
// the individual responses because the order of entries written to APP_DB by
// p4rt does not match the order in which the entries are pulled out from
// APP_DB. Hence, we expect to see the expected responses but not in the same
// order.
absl::Status GetAppDbResponses(
    int expected_response_count,
    swss::ConsumerNotifierInterface& notification_interface,
    absl::flat_hash_map<std::string, pdpi::IrUpdateStatus>& responses_map) {
  // Loop through and get the expected notification responses from Orchagent,
  // max timeout 10 minutes. OrchAgent sends the status code as string in the
  // op, key as data and the actual table entries as value_tuples.
  for (int i = 0; i < expected_response_count; i++) {
    std::string status_str;
    std::string actual_key;
    std::vector<swss::FieldValueTuple> value_tuples;

    if (!notification_interface.WaitForNotificationAndPop(
            status_str, actual_key, value_tuples, /*timeout_ms=*/10 * 60000)) {
      return gutil::InternalErrorBuilder()
             << "Timeout or other errors on waiting for Appl DB response from "
                "OrchAgent";
    }
    if (value_tuples.empty()) {
      return gutil::InternalErrorBuilder()
             << "Notification response for '" << actual_key
             << "' should not be empty.";
    }

    pdpi::IrUpdateStatus result;
    // The first element in the values vector is the detailed error message in
    // the form of ("err_str", <error message>).
    const swss::FieldValueTuple& first_tuple = value_tuples[0];
    if (fvField(first_tuple) != "err_str") {
      return gutil::InternalErrorBuilder()
             << "The response path expects the first field value to be "
             << "'err_str', but the OrchAgent has responsed with '"
             << fvField(first_tuple) << "'.";
    } else {
      result.set_code(SwssToP4RTErrorCode(status_str));
      result.set_message(fvValue(first_tuple));
    }

    // Insert into the responses map.
    RETURN_IF_ERROR(gutil::InsertIfUnique(
        responses_map, actual_key, result,
        absl::StrCat("Found several keys with the same name: ", actual_key,
                     ", batch count: ", expected_response_count)));
  }
  return absl::OkStatus();
}

// Restore APPL_DB to the last successful state.
absl::Status RestoreApplDb(const std::string& key,
                           swss::DBConnectorInterface& app_db_client,
                           swss::DBConnectorInterface& state_db_client) {
  // Query the APPL_STATE_DB with the same key as in APPL_DB.
  std::unordered_map<std::string, std::string> values_map =
      state_db_client.hgetall(key);
  if (values_map.empty()) {
    // No entry in APPL_STATE_DB with this key indicates this is an insert
    // operation that has to be restored, which then has to be removed.
    LOG(INFO) << "Restoring (by delete) AppDb entry: " << key;
    auto del_entries = app_db_client.del(key);
    RET_CHECK(del_entries == 1)
        << "Unexpected number of delete entries when tring to delete a newly "
           "added entry from ApplDB for a failed response, expected : 1, "
           "actual: "
        << del_entries;
    return absl::OkStatus();
  }

  std::vector<swss::FieldValueTuple> value_tuples;
  value_tuples.resize(values_map.size());
  int i = 0;
  for (auto& entry : values_map) {
    value_tuples.at(i++) = entry;
  }
  // Update APPL_DB with the retrieved values from APPL_STATE_DB.
  LOG(INFO) << "Restoring (by update) AppDb entry: " << key;
  app_db_client.del(key);
  app_db_client.hmset(key, value_tuples);

  return absl::OkStatus();
}

}  // namespace

absl::Status GetAndProcessResponseNotification(
    absl::Span<const std::string> keys, int expected_response_count,
    swss::ConsumerNotifierInterface& notification_interface,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    pdpi::IrWriteResponse& ir_write_response) {
  // Accumulate all critical state error messages.
  std::stringstream critical_errors;
  const auto number_update_statuses =
      static_cast<uint32_t>(ir_write_response.statuses_size());
  if (number_update_statuses > keys.size()) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Number of response statuses: " << number_update_statuses
           << " cannot be greater than the number of keys: " << keys.size();
  }

  absl::flat_hash_map<std::string, pdpi::IrUpdateStatus> responses_map;
  // Get the expected number of responses from the notification channel.
  auto status = GetAppDbResponses(expected_response_count,
                                  notification_interface, responses_map);
  if (!status.ok()) {
    critical_errors << status.ToString() << "\n";
  }

  // Add as many empty IrUpdateStatus entries as in keys vector if caller didnt
  // allocate.
  for (uint32_t i = 0; i < keys.size() - number_update_statuses; i++) {
    ir_write_response.add_statuses();
  }

  int index = 0;
  // Iterate and update the statuses protobuf for every response.
  for (auto& update_status : *ir_write_response.mutable_statuses()) {
    // Look only for responses that were written into APP_DB.
    if (!keys[index].empty()) {
      // Remove the table name from expected_key before checking in the
      // responses map.
      const std::string key =
          std::string(keys[index].substr(keys[index].find(":") + 1));
      // Lookup into the reponses map to get the response value.
      auto* response = gutil::FindOrNull(responses_map, key);
      if (response == nullptr) {
        // Failed to get response for the key, update internal error in the
        // status to be sent to controller.
        std::string error_str =
            absl::StrCat("Failed to get response from OrchAgent for key ",
                         keys[index], " error: timeout or other errors");
        update_status.set_code(Code::INTERNAL);
        update_status.set_message(error_str);
      } else {
        // Got a response but the result can be OK or NOTOK.
        // if OK, nothing further to do.
        // else the previous values of the table entry has to be restored.
        // std::pair<Code, std::string> result = response_or.value();
        update_status.set_code(response->code());
        if (update_status.code() != Code::OK) {
          update_status.set_message(response->message());
          LOG(ERROR) << "Got an unexpected response for key " << key
                     << " error : " << update_status.code()
                     << " error details : " << update_status.message();

          // If error, restore the APPL_DB by querying the values for the same
          // key in APPL_STATE_DB, as that would hold the last programmed
          // value in the hardware.
          auto restore_status =
              RestoreApplDb(keys[index], app_db_client, state_db_client);
          if (!restore_status.ok()) {
            critical_errors << "Restore Appl Db for key " << key
                            << " failed, error : " << restore_status.message()
                            << "\n";
          }
        }
      }
    }
    index++;
  }

  if (critical_errors.str().empty()) {
    return absl::OkStatus();
  } else {
    return gutil::InternalErrorBuilder().LogError() << critical_errors.str();
  }
}

}  // namespace sonic
}  // namespace p4rt_app

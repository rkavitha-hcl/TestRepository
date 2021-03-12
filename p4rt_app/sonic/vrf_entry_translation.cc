// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "p4rt_app/sonic/vrf_entry_translation.h"

#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "glog/logging.h"
#include "gutil/status.h"
#include "p4rt_app/sonic/response_handler.h"

namespace p4rt_app {
namespace sonic {
namespace {

// The paramter (match or action failed name) that represents a VRF ID. This is
// fixed at compile time.
constexpr char kVrfIdParamName[] = "vrf_id";

absl::optional<std::string> GetVrfId(const pdpi::IrTableEntry& ir_table_entry) {
  // Check all match parameters for a VRF ID.
  for (const auto& param : ir_table_entry.matches()) {
    if (param.name() == kVrfIdParamName) {
      return param.exact().str();
    }
  }

  // Check all action parameters for a VRF ID.
  for (const auto& param : ir_table_entry.action().params()) {
    if (param.name() == kVrfIdParamName) {
      return param.value().str();
    }
  }
  return {};
}

absl::optional<std::string> GetVrfId(
    const std::unordered_map<std::string, std::string>& app_db_values) {
  auto iter = app_db_values.find(kVrfIdParamName);
  if (iter == app_db_values.end()) {
    return {};
  }
  return iter->second;
}

absl::Status DoInsert(swss::ProducerStateTableInterface& vrf_table,
                      swss::ConsumerNotifierInterface& vrf_notification,
                      swss::DBConnectorInterface& app_db_client,
                      swss::DBConnectorInterface& state_db_client,
                      const absl::optional<std::string>& vrf_id,
                      absl::flat_hash_map<std::string, int>* reference_count) {
  RET_CHECK(reference_count != nullptr) << "reference_count cannot be nullptr.";

  // If the request doesn't have a VRF ID then there is nothing to do.
  if (!vrf_id.has_value()) return absl::OkStatus();

  // If the request is using the default VRF ID then there is nothing to do.
  if (vrf_id.value().empty()) return absl::OkStatus();

  // If the VRF ID is already used by another table entry then we only increment
  // the reference count.
  auto reference = reference_count->find(*vrf_id);
  if (reference != reference_count->end()) {
    reference->second++;
    return absl::OkStatus();
  }

  // Otherwise we need to add the VRF ID to the SONiC VRF table.
  LOG(INFO) << "Create VRF: " << *vrf_id;
  const std::vector<swss::FieldValueTuple> values = {
      std::make_pair("ip_opt_action", "trap"),
      std::make_pair("l3_mc_action", "drop"),
      std::make_pair("ttl_action", "trap"),
      std::make_pair("src_mac", "00:AA:BB:CC:DD:EE"),
      std::make_pair("v4", "false"),
      std::make_pair("v6", "true"),
  };
  vrf_table.set(*vrf_id, values);

  // Verify VRF is successfully programmed through the response path.
  // Because new VRF's are rare this will be a blocking call that waits for a
  // notification from OrchAgent.
  pdpi::IrWriteResponse ir_write_response;
  RETURN_IF_ERROR(GetAndProcessResponseNotification(
      std::vector<std::string>(
          {absl::StrCat(vrf_table.get_table_name(), ":", *vrf_id)}),
      /*expected_response_count=*/1, vrf_notification, app_db_client,
      state_db_client, ir_write_response));
  // Verify that the set operation succeeded.
  absl::Status status(
      static_cast<absl::StatusCode>(ir_write_response.statuses(0).code()),
      ir_write_response.statuses(0).message());
  if (status.ok()) {
    reference_count->insert(std::make_pair(*vrf_id, 1));
  }

  return status;
}

absl::Status DoDecrement(
    swss::ProducerStateTableInterface& vrf_table,
    const absl::optional<std::string>& vrf_id,
    absl::flat_hash_map<std::string, int>* reference_count) {
  RET_CHECK(reference_count != nullptr) << "reference_count cannot be nullptr.";

  // If the request doesn't have a VRF ID then there is nothing to do.
  if (!vrf_id.has_value()) return absl::OkStatus();

  // If the request is using the default VRF ID then there is nothing to do.
  if (vrf_id.value().empty()) return absl::OkStatus();

  // If we cannot find the reference count then something is wrong.
  auto reference = reference_count->find(*vrf_id);
  if (reference == reference_count->end()) {
    LOG(ERROR) << "We are trying to delete VRF '" << *vrf_id
               << "', but it does not exist in the internal state.";
    return gutil::InternalErrorBuilder()
           << "VRF " << *vrf_id << " does not exist.";
  }
  reference->second--;
  return absl::OkStatus();
}

}  // namespace

absl::Status PruneVrfReferences(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    absl::flat_hash_map<std::string, int>* reference_count) {
  std::vector<std::string> keys;
  for (const auto& [vrf_id, count] : *reference_count) {
    // If another table entry still uses this VRF ID then nothing to do.
    if (count > 0) {
      continue;
    }

    // Otherwise we can delete the VRF ID from the SONiC VRF table.
    LOG(INFO) << "Unused VRF " << vrf_id << " being deleted from APP_DB";
    vrf_table.del(vrf_id);
    keys.push_back(absl::StrCat(vrf_table.get_table_name(), ":", vrf_id));
  }

  // If no VRF was identified for deletion, nothing to do.
  if (keys.empty()) {
    return absl::OkStatus();
  }

  // Wait and process response from OrchAgent for VRF entry deletion.
  pdpi::IrWriteRpcStatus rpc_status;
  pdpi::IrWriteResponse* rpc_response = rpc_status.mutable_rpc_response();
  RETURN_IF_ERROR(GetAndProcessResponseNotification(
                      keys, keys.size(), vrf_notification, app_db_client,
                      state_db_client, *rpc_response))
          .SetPrepend()
      << "Orchagent could not handle the VRF deletion: ";

  // Verify that the delete operations succeeded.
  std::vector<std::string> vrf_errors;
  int i = 0;
  for (const auto& resp_status : rpc_response->statuses()) {
    if (resp_status.code() != google::rpc::Code::OK) {
      vrf_errors.push_back(resp_status.message());
    } else {
      // Remove the VRF_TABLE prefix in the key.
      reference_count->erase(keys[i].substr(keys[i].find(':') + 1));
    }
    i++;
  }

  if (!vrf_errors.empty()) {
    return gutil::InternalErrorBuilder() << "OrchAgent failed to delete VRF: "
                                         << absl::StrJoin(vrf_errors, "\n");
  }

  return absl::OkStatus();
}

absl::Status InsertVrfEntryAndUpdateState(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>* reference_count) {
  return DoInsert(vrf_table, vrf_notification, app_db_client, state_db_client,
                  GetVrfId(ir_table_entry), reference_count);
}

absl::Status DecrementVrfReferenceCount(
    swss::ProducerStateTableInterface& vrf_table,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>* reference_count) {
  return DoDecrement(vrf_table, GetVrfId(ir_table_entry), reference_count);
}

absl::Status ModifyVrfEntryAndUpdateState(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    const std::unordered_map<std::string, std::string> app_db_values,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>* reference_count) {
  absl::optional<std::string> vrf_id_to_remove = GetVrfId(app_db_values);
  absl::optional<std::string> vrf_id_to_insert = GetVrfId(ir_table_entry);

  // If neither entry has a VRF ID then there is nothing to do.
  if (!vrf_id_to_remove.has_value() && !vrf_id_to_insert.has_value()) {
    return absl::OkStatus();
  }

  // If both have the same VRF ID value then there is nothing to do.
  if (vrf_id_to_remove.has_value() && vrf_id_to_insert.has_value() &&
      vrf_id_to_remove == vrf_id_to_insert) {
    return absl::OkStatus();
  }

  // Otherwise, delete the VRF_ID in the AppDb, and insert the VRF ID from the
  // entry.
  RETURN_IF_ERROR(DoDecrement(vrf_table, vrf_id_to_remove, reference_count));
  auto status = DoInsert(vrf_table, vrf_notification, app_db_client,
                         state_db_client, vrf_id_to_insert, reference_count);
  // If the new vrf id insertion failed, increment the old vrf id reference
  // count since the referenced table entry will not be replaced.
  if (!status.ok()) {
    auto reference = reference_count->find(*vrf_id_to_remove);
    if (reference != reference_count->end()) reference->second++;
  }
  return status;
}

}  // namespace sonic
}  // namespace p4rt_app

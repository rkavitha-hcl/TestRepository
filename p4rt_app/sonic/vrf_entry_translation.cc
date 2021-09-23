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

#include <string>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "google/rpc/code.pb.h"
#include "gutil/status.h"
#include "p4_pdpi/ir.pb.h"
#include "p4rt_app/sonic/response_handler.h"
#include "p4rt_app/utils/status_utility.h"
#include "swss/rediscommand.h"

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

// Today VRF is only used for matching.
std::vector<swss::FieldValueTuple> GetVrfValues() {
  return std::vector<swss::FieldValueTuple>{
      std::make_pair("v4", "false"),
      std::make_pair("v6", "true"),
  };
}

absl::Status DoInsert(swss::ProducerStateTableInterface& vrf_table,
                      swss::ConsumerNotifierInterface& vrf_notification,
                      swss::DBConnectorInterface& app_db_client,
                      swss::DBConnectorInterface& state_db_client,
                      const absl::optional<std::string>& vrf_id,
                      absl::flat_hash_map<std::string, int>& reference_count) {
  // If the request doesn't have a VRF ID then there is nothing to do.
  if (!vrf_id.has_value()) return absl::OkStatus();

  // If the request is using the default VRF ID then there is nothing to do.
  // TODO: remove
  if (vrf_id.value().empty()) return absl::OkStatus();

  // If the VRF ID is already used by another table entry then we only increment
  // the reference count.
  auto reference = reference_count.find(*vrf_id);
  if (reference != reference_count.end()) {
    reference->second++;
    return absl::OkStatus();
  }

  // Otherwise we need to add the VRF ID to the SONiC VRF table.
  LOG(INFO) << "Create VRF: " << *vrf_id;
  vrf_table.set(*vrf_id, GetVrfValues());

  // Verify VRF is successfully programmed through the response path.
  // Because new VRF's are rare this will be a blocking call that waits for a
  // notification from OrchAgent.
  ASSIGN_OR_RETURN(pdpi::IrUpdateStatus status,
                   GetAndProcessResponseNotification(
                       vrf_table.get_table_name(), vrf_notification,
                       app_db_client, state_db_client, *vrf_id));

  // Verify that the set operation succeeded.
  if (status.code() == google::rpc::OK) {
    reference_count.insert(std::make_pair(*vrf_id, 1));
  }
  return gutil::ToAbslStatus(status);
}

absl::Status DoDecrement(
    swss::ProducerStateTableInterface& vrf_table,
    const absl::optional<std::string>& vrf_id,
    absl::flat_hash_map<std::string, int>& reference_count) {
  // If the request doesn't have a VRF ID then there is nothing to do.
  if (!vrf_id.has_value()) return absl::OkStatus();

  // If the request is using the default VRF ID then there is nothing to do.
  // TODO: remove
  if (vrf_id.value().empty()) return absl::OkStatus();

  // If we cannot find the reference count then something is wrong.
  auto reference = reference_count.find(*vrf_id);
  if (reference == reference_count.end()) {
    LOG(ERROR) << "We are trying to delete VRF '" << *vrf_id
               << "', but it does not exist in the internal state.";
    return gutil::InternalErrorBuilder()
           << "VRF " << *vrf_id << " does not exist.";
  }
  reference->second--;
  return absl::OkStatus();
}

absl::StatusOr<std::string> GetVrfTableKey(const pdpi::IrTableEntry& entry) {
  const std::string kVrfIdMatchField = "vrf_id";

  for (const auto& match : entry.matches()) {
    if (match.name() != kVrfIdMatchField) continue;

    // We are not allowed to touch SONiC's default VRF which is represented as
    // an empty string.
    if (match.exact().str().empty()) {
      return gutil::InvalidArgumentErrorBuilder()
             << "Operations on the Default VRF '" << match.exact().str()
             << "' are not allowed.";
    }
    return match.exact().str();
  }
  return gutil::InvalidArgumentErrorBuilder()
         << "Could not find match field '" << kVrfIdMatchField
         << "' in VRF_TABLE entry.";
}

absl::StatusOr<std::string> InsertVrfTableEntry(
    const pdpi::IrTableEntry& entry,
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client) {
  LOG(INFO) << "Insert PDPI IR entry: " << entry.ShortDebugString();
  ASSIGN_OR_RETURN(std::string key, GetVrfTableKey(entry));

  // Check that key does not already exist in the table.
  std::string full_key = absl::StrCat(vrf_table.get_table_name(), ":", key);
  if (app_db_client.exists(full_key)) {
    LOG(WARNING) << "Could not insert duplicate VRF_TABLE entry: " << key;
    return gutil::AlreadyExistsErrorBuilder()
           << "[P4RT App] Table entry with key '" << full_key
           << "' already exist in '" << entry.table_name() << "'.";
  }

  LOG(INFO) << "Insert VRF_TABLE entry: " << key;
  vrf_table.set(key, GetVrfValues());
  return key;
}

absl::StatusOr<std::string> DeleteVrfTableEntry(
    const pdpi::IrTableEntry& entry,
    swss::ProducerStateTableInterface& vrf_table,
    swss::DBConnectorInterface& app_db_client) {
  LOG(INFO) << "Delete PDPI IR entry: " << entry.ShortDebugString();
  ASSIGN_OR_RETURN(std::string key, GetVrfTableKey(entry));

  std::string full_key = absl::StrCat(vrf_table.get_table_name(), ":", key);
  // Check that key exists in the table.
  if (!app_db_client.exists(full_key)) {
    LOG(WARNING) << "Could not delete missing VRF_TABLE entry: " << key;
    return gutil::NotFoundErrorBuilder()
           << "[P4RT App] Table entry with key '" << full_key
           << "' does not exist in '" << entry.table_name() << "'.";
  }

  LOG(INFO) << "Delete VRF_TABLE entry: " << key;
  vrf_table.del(key);
  return key;
}

}  // namespace

absl::Status PruneVrfReferences(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    absl::flat_hash_map<std::string, int>& reference_count) {
  pdpi::IrWriteResponse update_status;
  absl::btree_map<std::string, pdpi::IrUpdateStatus*> status_by_key;
  for (const auto& [vrf_id, count] : reference_count) {
    // If another table entry still uses this VRF ID then nothing to do.
    if (count > 0) {
      continue;
    }

    // Otherwise we can delete the VRF ID from the SONiC VRF table.
    LOG(INFO) << "Unused VRF " << vrf_id << " being deleted from APP_DB";
    vrf_table.del(vrf_id);
    status_by_key[vrf_id] = update_status.add_statuses();
  }

  // If no VRF was identified for deletion, nothing to do.
  if (status_by_key.empty()) {
    return absl::OkStatus();
  }

  // Wait and process response from OrchAgent for VRF entry deletion.
  RETURN_IF_ERROR(GetAndProcessResponseNotification(
      vrf_table.get_table_name(), vrf_notification, app_db_client,
      state_db_client, status_by_key));

  // Verify that the delete operations succeeded.
  std::vector<std::string> vrf_errors;
  for (const auto& [key, status] : status_by_key) {
    if (status->code() != google::rpc::Code::OK) {
      vrf_errors.push_back(status->message());
    } else {
      // Remove the VRF_TABLE prefix in the key.
      reference_count.erase(key);
    }
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
    absl::flat_hash_map<std::string, int>& reference_count) {
  return DoInsert(vrf_table, vrf_notification, app_db_client, state_db_client,
                  GetVrfId(ir_table_entry), reference_count);
}

absl::Status DecrementVrfReferenceCount(
    swss::ProducerStateTableInterface& vrf_table,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>& reference_count) {
  return DoDecrement(vrf_table, GetVrfId(ir_table_entry), reference_count);
}

absl::Status ModifyVrfEntryAndUpdateState(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    const std::unordered_map<std::string, std::string> app_db_values,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>& reference_count) {
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
    auto reference = reference_count.find(*vrf_id_to_remove);
    if (reference != reference_count.end()) reference->second++;
  }
  return status;
}

absl::Status UpdateAppDbVrfTable(
    p4::v1::Update::Type update_type, int rpc_index,
    const pdpi::IrTableEntry& entry,
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    pdpi::IrWriteResponse& response) {
  absl::StatusOr<std::string> update_key;
  switch (update_type) {
    case p4::v1::Update::INSERT:
      update_key = InsertVrfTableEntry(entry, vrf_table, vrf_notification,
                                       app_db_client);
      break;
    case p4::v1::Update::MODIFY:
      update_key = gutil::InvalidArgumentErrorBuilder()
                   << "Modifing VRF_TABLE entries is not allowed.";
      break;
    case p4::v1::Update::DELETE:
      update_key = DeleteVrfTableEntry(entry, vrf_table, app_db_client);
      break;
    default:
      update_key = gutil::InvalidArgumentErrorBuilder()
                   << "Unsupported update type: " << update_type;
  }

  if (update_key.ok()) {
    ASSIGN_OR_RETURN(*response.mutable_statuses(rpc_index),
                     GetAndProcessResponseNotification(
                         vrf_table.get_table_name(), vrf_notification,
                         app_db_client, state_db_client, *update_key));
  } else {
    LOG(WARNING) << "Could not update in AppDb: " << update_key.status();
    *response.mutable_statuses(rpc_index) =
        GetIrUpdateStatus(update_key.status());
  }

  return absl::OkStatus();
}

absl::StatusOr<std::vector<pdpi::IrTableEntry>> GetAllAppDbVrfTableEntries(
    swss::DBConnectorInterface& app_db_client) {
  std::vector<pdpi::IrTableEntry> vrf_entries;

  for (const std::string& key : app_db_client.keys("*")) {
    const std::vector<std::string> split = absl::StrSplit(key, ':');
    if (split.size() < 2) continue;

    // The VRF_TABLE entries will either start with "_VRF_TABLE" (if orchagent
    // has not installed the entry) or "VRF_TABLE" (if orchagent has installed
    // the entry). When getting the VRF_TABLE entries we are only concerned with
    // what orchagent has installed.
    if (split[0] != "VRF_TABLE") continue;

    // TODO: "p4rt-" prefix should not be filitered out.
    if (absl::StartsWith(split[1], "p4rt-")) continue;

    VLOG(1) << "Read AppDb entry: " << key;
    pdpi::IrTableEntry table_entry;
    // Fixed table name.
    table_entry.set_table_name("vrf_table");
    // Fixed match field name.
    auto* match = table_entry.add_matches();
    match->set_name("vrf_id");
    match->mutable_exact()->set_str(split[1]);
    // Fixed action.
    table_entry.mutable_action()->set_name("no_action");

    vrf_entries.push_back(table_entry);
  }

  return vrf_entries;
}

}  // namespace sonic
}  // namespace p4rt_app

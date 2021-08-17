// Copyright 2021 Google LLC
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
#include "p4rt_app/sonic/hashing.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "glog/logging.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "p4_pdpi/utils/annotation_parser.h"
#include "p4rt_app/sonic/response_handler.h"
#include "swss/json.h"
#include "swss/json.hpp"

namespace p4rt_app {
namespace sonic {
namespace {

using ::gutil::InvalidArgumentErrorBuilder;

// Generate the JSON format for HASH_TABLE entries with sai_ecmp_hash and
// sai_native_hash_field annotations.
// @sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IPV4)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV4)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV4)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)
//
// JSON:
// “HASH_TABLE:hash_ipv4_config” = {
//    “hash_field_list”: [“src_ip”, “dst_ip”, “l4_src_port”, “l4_dst_port”,
//                        “ip_protocol”],
// }
absl::StatusOr<nlohmann::json> GenerateJsonHashFieldEntries(
    const std::vector<std::vector<std::string>>& parse_results) {
  const absl::flat_hash_map<std::string, std::string> hash_fields_map = {
      {"SAI_NATIVE_HASH_FIELD_SRC_IPV4", "src_ip"},
      {"SAI_NATIVE_HASH_FIELD_DST_IPV4", "dst_ip"},
      {"SAI_NATIVE_HASH_FIELD_SRC_IPV6", "src_ip"},
      {"SAI_NATIVE_HASH_FIELD_DST_IPV6", "dst_ip"},
      {"SAI_NATIVE_HASH_FIELD_L4_SRC_PORT", "l4_src_port"},
      {"SAI_NATIVE_HASH_FIELD_L4_DST_PORT", "l4_dst_port"},
      {"SAI_NATIVE_HASH_FIELD_IPV6_FLOW_LABEL", "ipv6_flow_label"},
  };

  nlohmann::json json;

  for (const auto& fields : parse_results) {
    if (fields.size() != 1) {
      return InvalidArgumentErrorBuilder()
             << "Unexpected number of native hash field specified: "
             << "expected 1, actual " << fields.size();
    }
    ASSIGN_OR_RETURN(
        auto field_value, gutil::FindOrStatus(hash_fields_map, fields.at(0)),
        _ << "Unable to find hash field string for " << fields.at(0));
    json.push_back(field_value);
  }

  return json;
}

}  // namespace

bool IsIpv4HashKey(absl::string_view key) {
  return absl::StrContains(key, "ipv4");
}

bool IsIpv6HashKey(absl::string_view key) {
  return absl::StrContains(key, "ipv6");
}

// Generate the APP_DB format for hash field entries to be written to
// HASH_TABLE.
absl::StatusOr<std::vector<EcmpHashEntry>> GenerateAppDbHashFieldEntries(
    const pdpi::IrP4Info& ir_p4info) {
  std::vector<EcmpHashEntry> hash_entries;
  for (const auto& actions : ir_p4info.actions_by_name()) {
    auto parse_results = pdpi::GetAllAnnotationsAsArgList(
        "sai_native_hash_field", actions.second.preamble().annotations());
    if (!parse_results.ok()) continue;
    auto json = GenerateJsonHashFieldEntries(*parse_results);
    if (!json.ok()) {
      return InvalidArgumentErrorBuilder()
             << "Unable to generate hash field action annotation entries "
             << json.status();
    } else {
      hash_entries.push_back(EcmpHashEntry(
          {actions.first, std::vector<swss::FieldValueTuple>(
                              {{"hash_field_list", (*json).dump()}})}));
    }
  }
  if (hash_entries.empty()) {
    return InvalidArgumentErrorBuilder()
           << "Missing hash field entries in P4Info file.";
  }
  return hash_entries;
}

absl::StatusOr<std::vector<swss::FieldValueTuple>>
GenerateAppDbHashValueEntries(const pdpi::IrP4Info& ir_p4info) {
  static constexpr char kEcmpHashAlg[] = "ecmp_hash_algorithm";
  static constexpr char kEcmpHashSeed[] = "ecmp_hash_seed";
  static constexpr char kEcmpHashOffset[] = "ecmp_hash_offset";
  const absl::flat_hash_map<std::string, std::string> hash_alg_map = {
      {"SAI_HASH_ALGORITHM_CRC_32LO", "crc_32lo"},
  };

  absl::flat_hash_set<std::string> hash_values_set;
  std::vector<swss::FieldValueTuple> hash_value_entries;
  for (const auto& actions : ir_p4info.actions_by_name()) {
    auto parse_results = pdpi::GetAllAnnotationsAsArgList(
        "sai_hash_algorithm", actions.second.preamble().annotations());
    if (!parse_results.ok()) continue;
    // Expect to get all hashing value related annotations like algorithm,
    // offset, seed etc.
    const auto hash_components =
        pdpi::GetAllAnnotations(actions.second.preamble().annotations());
    if (hash_components.empty()) {
      return InvalidArgumentErrorBuilder()
             << "No entries for hash algorithm, offset, seed";
    }
    for (const auto& hash_value : hash_components) {
      if (hash_value.label == "sai_hash_algorithm") {
        ASSIGN_OR_RETURN(auto alg_type,
                         gutil::FindOrStatus(hash_alg_map, hash_value.body),
                         _ << "Unable to find hash algorithm string for "
                           << hash_value.body);
        RETURN_IF_ERROR(gutil::InsertIfUnique(
            hash_values_set, std::string(kEcmpHashAlg),
            absl::StrCat("Duplicate hash algorithm type specified.")));
        hash_value_entries.push_back(
            swss::FieldValueTuple({kEcmpHashAlg, alg_type}));
      } else if (hash_value.label == "sai_hash_seed") {
        RETURN_IF_ERROR(gutil::InsertIfUnique(
            hash_values_set, std::string(kEcmpHashSeed),
            absl::StrCat("Duplicate hash algorithm seed specified.")));
        hash_value_entries.push_back(
            swss::FieldValueTuple({kEcmpHashSeed, hash_value.body}));
      } else if (hash_value.label == "sai_hash_offset") {
        RETURN_IF_ERROR(gutil::InsertIfUnique(
            hash_values_set, std::string(kEcmpHashOffset),
            absl::StrCat("Duplicate hash algorithm offset specified.")));
        hash_value_entries.push_back(
            swss::FieldValueTuple({kEcmpHashOffset, hash_value.body}));
      } else {
        return InvalidArgumentErrorBuilder()
               << "Not a valid hash value label: " << hash_value.label;
      }
    }
  }
  if (hash_value_entries.empty()) {
    return InvalidArgumentErrorBuilder()
           << "Missing hash value entries in P4Info file.";
  }

  return hash_value_entries;
}

absl::StatusOr<std::vector<std::string>> ProgramHashFieldTable(
    const pdpi::IrP4Info& ir_p4info,
    swss::ProducerStateTableInterface& app_db_table_hash,
    swss::ConsumerNotifierInterface& app_db_notifier_hash,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client) {
  // Get the key, value pairs of Hash field APP_DB entries.
  ASSIGN_OR_RETURN(const auto entries,
                   sonic::GenerateAppDbHashFieldEntries(ir_p4info));
  std::vector<std::string> keys;
  // Write to APP_DB.
  for (const auto& entry : entries) {
    app_db_table_hash.set(entry.hash_key, entry.hash_value);
    keys.push_back(
        absl::StrCat(app_db_table_hash.get_table_name(), ":", entry.hash_key));
  }

  pdpi::IrWriteResponse ir_write_response;
  RETURN_IF_ERROR(sonic::GetAndProcessResponseNotification(
      keys, keys.size(), app_db_notifier_hash, app_db_client, state_db_client,
      ir_write_response));
  // Pickup the hash field keys that were written(and ack'ed) successfully by
  // OrchAgent.
  std::vector<std::string> hash_field_keys;
  int i = 0;
  for (const auto& response : ir_write_response.statuses()) {
    if (response.code() == google::rpc::Code::OK) {
      // Add to valid set of hash field keys (strip the table name prefix).
      hash_field_keys.push_back(keys.at(i).substr(keys.at(i).find(':') + 1));
    } else {
      return gutil::InternalErrorBuilder()
             << "Got an error from Orchagent for hash field: " << keys.at(i)
             << "error: " << response.message();
    }
    i++;
  }
  return hash_field_keys;
}

absl::Status ProgramSwitchTable(
    const pdpi::IrP4Info& ir_p4info, std::vector<std::string> hash_fields,
    swss::ProducerStateTableInterface& app_db_table_switch,
    swss::ConsumerNotifierInterface& app_db_notifier_switch,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client) {
  static constexpr absl::string_view kSwitchTableEntryKey = "switch";
  std::vector<swss::FieldValueTuple> switch_table_attrs;
  // Get all the hash value related attributes like algorithm type, offset and
  // seed value etc.
  ASSIGN_OR_RETURN(switch_table_attrs,
                   sonic::GenerateAppDbHashValueEntries(ir_p4info));

  // Add the ecmp hash fields for Ipv4 & Ipv6.
  for (const auto& hash_field_key : hash_fields) {
    if (IsIpv4HashKey(hash_field_key)) {
      switch_table_attrs.push_back(
          swss::FieldValueTuple({"ecmp_hash_ipv4", hash_field_key}));
    } else if (IsIpv6HashKey(hash_field_key)) {
      switch_table_attrs.push_back(
          swss::FieldValueTuple({"ecmp_hash_ipv6", hash_field_key}));
    } else {
      return InvalidArgumentErrorBuilder()
             << "Invalid hash field key: " << hash_field_key;
    }
  }

  // Write to switch table and process response.
  std::vector<std::string> keys;
  app_db_table_switch.set(/*key=*/std::string(kSwitchTableEntryKey),
                          switch_table_attrs);
  keys.push_back(absl::StrCat(app_db_table_switch.get_table_name(), ":",
                              kSwitchTableEntryKey));
  pdpi::IrWriteResponse ir_write_response;
  RETURN_IF_ERROR(sonic::GetAndProcessResponseNotification(
      keys, keys.size(), app_db_notifier_switch, app_db_client, state_db_client,
      ir_write_response));
  for (int i = 0; i < ir_write_response.statuses().size(); i++) {
    if (ir_write_response.statuses(i).code() != google::rpc::Code::OK) {
      return gutil::InternalErrorBuilder()
             << "Got an error from Orchagent for SWITCH_TABLE: " << keys.at(i)
             << "error: " << ir_write_response.statuses(i).message();
    }
  }
  return absl::OkStatus();
}

}  // namespace sonic
}  // namespace p4rt_app

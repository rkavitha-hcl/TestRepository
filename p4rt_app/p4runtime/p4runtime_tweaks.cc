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

#include "p4rt_app/p4runtime/p4runtime_tweaks.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "p4_pdpi/utils/annotation_parser.h"

namespace p4rt_app {
namespace {

std::string MacAddrToIpv6LinkLocal(const std::vector<std::string>& mac_addr) {
  // Flip the 6th bit in the first octet.
  uint32_t first_octet = std::stoul(mac_addr[0], nullptr, 16) ^ 0x02;

  // TODO: switch string abls::Format() once we figure out the
  // compile error.
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(2) << first_octet;

  // prepend with "fe80::", and insert "ff:fe" between the mac address.
  return absl::Substitute("fe80::$0$1:$2ff:fe$3:$4$5", ss.str(), mac_addr[1],
                          mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

std::string GetMapKey(const absl::flat_hash_map<std::string, std::string>& map,
                      const std::string& value) {
  // The reverse lookup has poor performance (i.e. iterative search). However,
  // since this is only temporary code we're trading off performace for simpler
  // state handling (i.e. not having duplicate maps).
  for (const auto& m : map) {
    if (m.second == value) return m.first;
  }

  // If we can't find the old value, then just default to the existing.
  return value;
}

bool IsPortName(absl::string_view name) {
  return (name == "port") || (name == "watch_port") || (name == "in_port") ||
         (name == "out_port") || (name == "dst_port");
}

void SetPortMatchFieldFormatToString(pdpi::IrMatchFieldDefinition& match_def,
                                     bool log) {
  if (!IsPortName(match_def.match_field().name())) return;
  if (log) {
    LOG(WARNING) << "Updating match field '" << match_def.match_field().name()
                 << "' format to STRING.";
  }
  match_def.set_format(pdpi::Format::STRING);
}

void SetCompositeUdfFieldFormatToHexString(
    pdpi::IrMatchFieldDefinition& match_def) {
  static constexpr absl::string_view kCompositeMatchLabel = "composite_field";
  static constexpr absl::string_view kUdfMatchLabel = "sai_udf";

  if (match_def.format() == pdpi::Format::HEX_STRING) return;

  auto parse_result = pdpi::GetAnnotationAsArgList(
      kCompositeMatchLabel, match_def.match_field().annotations());
  if (!parse_result.ok()) return;     // Composite annotation not found.
  if (parse_result->empty()) return;  // No sub-fields.

  // Check if all sub-fields are UDF.
  for (const pdpi::annotation::AnnotationComponents& annotation :
       pdpi::GetAllAnnotations(*parse_result)) {
    if (annotation.label != kUdfMatchLabel) {
      return;
    }
  }

  match_def.set_format(pdpi::Format::HEX_STRING);
}

void TweakForOrchAgent(pdpi::IrTableDefinition& table_def) {
  for (auto& [match_id, match_def] : *table_def.mutable_match_fields_by_id()) {
    SetPortMatchFieldFormatToString(match_def, /*log=*/true);
    SetCompositeUdfFieldFormatToHexString(match_def);
  }
  for (auto& [match_name, match_def] :
       *table_def.mutable_match_fields_by_name()) {
    SetPortMatchFieldFormatToString(match_def, /*log=*/false);
    SetCompositeUdfFieldFormatToHexString(match_def);
  }
}

}  // namespace

pdpi::IrTableEntry P4RuntimeTweaks::ForOrchAgent(pdpi::IrTableEntry entry) {
  // Tweak neighbor_id for the NeighborTable:
  //   translate Controller's NeighborId (int) to IPv6 link local address based
  //   on the destination mac. Cache the Controller ID and link local address
  //   together.
  if (entry.table_name() == "neighbor_table") {
    std::vector<std::string> dst_mac;
    for (const auto& param : entry.action().params()) {
      if (param.name() == "dst_mac") {
        dst_mac = absl::StrSplit(param.value().mac(), ':');
      }
    }

    for (auto& param : *entry.mutable_matches()) {
      if (param.name() == "neighbor_id") {
        std::string link_local = MacAddrToIpv6LinkLocal(dst_mac);
        LOG(WARNING) << "Replacing neighbor_id " << param.exact().str()
                     << " with " << link_local << ".";

        // Cache the link local address so other tables can reference this one.
        neighbor_id_cache_.insert({param.exact().str(), link_local});
        param.mutable_exact()->set_str(link_local);
      }
    }
  }

  // Tweak neighbor_id for any action param:
  //   translate any Controller NeighborId (int) into a cached link local
  //   address.
  if (entry.has_action()) {
    for (auto& param : *entry.mutable_action()->mutable_params()) {
      if (param.name() == "neighbor_id") {
        std::string key = param.value().str();

        auto iter = neighbor_id_cache_.find(key);
        if (iter != neighbor_id_cache_.end()) {
          LOG(WARNING) << "Replacing neighbor_id " << key << " with "
                       << iter->second << ".";
          param.mutable_value()->set_str(iter->second);
        }
      }
    }
  }

  // Tweak vrf_id for any match field:
  //   overwrite any VRF_ID to use the default.
  //
  // TODO: remove once we install VRF IDs through pink-path.
  for (auto& match : *entry.mutable_matches()) {
    if (match.name() == "vrf_id" && match.exact().str() == "vrf-0") {
      LOG(WARNING) << "Replacing vrf_id " << match.exact().str()
                   << " with the default VRF.";
      *match.mutable_exact()->mutable_str() = "";
    }
  }

  return entry;
}

absl::StatusOr<pdpi::IrTableEntry> P4RuntimeTweaks::ForController(
    pdpi::IrTableEntry entry) {
  // Revert neighbor_id for the NeighborTable.
  if (entry.table_name() == "neighbor_table") {
    for (auto& param : *entry.mutable_matches()) {
      if (param.name() == "neighbor_id") {
        std::string pi_id = GetMapKey(neighbor_id_cache_, param.exact().str());
        LOG(WARNING) << "Reverting neighbor_id " << param.exact().str()
                     << " with " << pi_id << ".";
        param.mutable_exact()->set_str(pi_id);
      }
    }
  }

  // Revert neighbor_id for any action param.
  if (entry.has_action()) {
    for (auto& param : *entry.mutable_action()->mutable_params()) {
      if (param.name() == "neighbor_id") {
        std::string pi_id = GetMapKey(neighbor_id_cache_, param.value().str());
        LOG(WARNING) << "Reverting neighbor_id " << param.value().str()
                     << " with " << pi_id << ".";
        param.mutable_value()->set_str(pi_id);
      }
    }
  }

  // Revert vrf_id for any match field.
  // TODO: remove once we install VRF IDs through pink-path.
  for (auto& match : *entry.mutable_matches()) {
    if (match.name() == "vrf_id" && match.exact().str().empty()) {
      LOG(WARNING) << "Replacing vrf_id " << match.exact().str()
                   << " with vrf-0.";
      *match.mutable_exact()->mutable_str() = "vrf-0";
    }
  }

  return entry;
}

void P4RuntimeTweaks::ForOrchAgent(pdpi::IrP4Info& p4_info) {
  for (auto& [table_id, table_def] : *p4_info.mutable_tables_by_id()) {
    TweakForOrchAgent(table_def);
  }
  for (auto& [table_id, table_def] : *p4_info.mutable_tables_by_name()) {
    TweakForOrchAgent(table_def);
  }
}

}  // namespace p4rt_app

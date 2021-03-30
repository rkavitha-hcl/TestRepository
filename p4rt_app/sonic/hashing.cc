#include "p4rt_app/sonic/hashing.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "glog/logging.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "p4_pdpi/utils/annotation_parser.h"
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
      {"SAI_NATIVE_HASH_FIELD_SRC_IPV4", "src_ipv4"},
      {"SAI_NATIVE_HASH_FIELD_DST_IPV4", "dst_ipv4"},
      {"SAI_NATIVE_HASH_FIELD_SRC_IPV6", "src_ipv6"},
      {"SAI_NATIVE_HASH_FIELD_DST_IPV6", "dst_ipv6"},
      {"SAI_NATIVE_HASH_FIELD_L4_SRC_PORT", "l4_src_port"},
      {"SAI_NATIVE_HASH_FIELD_L4_DST_PORT", "l4_dst_port"},
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
                              {{"hash_fields_list", (*json).dump()}})}));
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
      {"SAI_HASH_ALGORITHM_CRC_32LO", "crc32_lo"},
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

}  // namespace sonic
}  // namespace p4rt_app

#ifndef _HASHING_H_
#define _HASHING_H_

#include <vector>

#include "absl/status/status.h"
#include "p4_pdpi/ir.h"
#include "swss/table.h"

namespace p4rt_app {
namespace sonic {

struct EcmpHashEntry {
  std::string hash_key;
  std::vector<swss::FieldValueTuple> hash_value;
};

// Generate the APP_DB format for hash field entries to be written to
// HASH_TABLE.
// sai_native_hash_field annotations.
// @sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IPV4)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV4)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV4)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)
// @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)
//
// “hash_ipv4_config” = {
//    “hash_field_list”: [“src_ip”, “dst_ip”, “l4_src_port”, “l4_dst_port”,
//                        “ip_protocol”],
//  }
absl::StatusOr<std::vector<EcmpHashEntry>> GenerateAppDbHashFieldEntries(
    const pdpi::IrP4Info& ir_p4info);

// Generate the APP_DB format for hash value entries to be written to
// SWITCH_TABLE.
// “switch”: {
//    “ecmp_hash_algorithm”: “crc32_lo”,  # SAI_HASH_ALGORITHM_CRC32_LO
//    “ecmp_hash_seed”: “10”,
//    "ecmp_hash_offset": "10"
// }
absl::StatusOr<std::vector<swss::FieldValueTuple>>
GenerateAppDbHashValueEntries(const pdpi::IrP4Info& ir_p4info);

}  // namespace sonic
}  // namespace p4rt_app

#endif  // _HASHING_H_

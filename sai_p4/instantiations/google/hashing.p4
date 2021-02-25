#ifndef SAI_HASHING_P4_
#define SAI_HASHING_P4_

#include <v1model.p4>
#include "../../fixed/headers.p4"
#include "../../fixed/metadata.p4"
#include "../../fixed/ids.h"
#include "../../fixed/resource_limits.p4"

control hashing(in headers_t headers,
                inout local_metadata_t local_metadata,
                in standard_metadata_t standard_metadata) {
  const bit<1> HASH_BASE = 1w0;
  const bit<14> HASH_MAX = 14w1024;

  // TODO: hide `select_emcp_hash_algorithm`, `compute_ecmp_hash_ipv4`,
  // and `compute_ecmp_hash_ipv6` from PD protos.

  // TODO: need to set these values differently for S2 and S3
  // S2 is SAI_HASH_ALGORITHM_CRC_CCITT with offset 4
  // S3 is SAI_HASH_ALGORITHM_CRC       with offset 8
  @sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC)
  @id(SELECT_ECMP_HASH_ALGORITHM_ACTION_ID)
  action select_emcp_hash_algorithm() {
    // TODO:
    // this action should set a local `hash_algorithm` variable to the hash
    // algorithm, e.g. `HashAlgorithm.crc32`, which would then be used by
    // `compute_ecmp_hash_ipv4` and `compute_ecmp_hash_ipv6`. However, BMv2 does
    // not support variables of Enum types at this point. BMv2 generates this
    // error:
    //
    //     type not yet handled on this target
    //
    //     enum HashAlgorithm {
    //          ^^^^^^^^^^^^^

    // TODO: need to figure out what to do with the offset.
  }

  @sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IPV4)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV4)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV4)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)
  @id(COMPUTE_ECMP_HASH_IPV4_ACTION_ID)
  action compute_ecmp_hash_ipv4() {
    hash(local_metadata.wcmp_selector_input,
         HashAlgorithm.crc32, HASH_BASE, {
         headers.ipv4.src_addr, headers.ipv4.dst_addr,
         local_metadata.l4_src_port, local_metadata.l4_dst_port},
         HASH_MAX);
  }

  @sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IPV6)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV6)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV6)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)
  @sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)
  // TODO: add flow label once supported.
  @id(COMPUTE_ECMP_HASH_IPV6_ACTION_ID)
  action compute_ecmp_hash_ipv6() {
    hash(local_metadata.wcmp_selector_input,
         HashAlgorithm.crc32, HASH_BASE, {
         headers.ipv6.flow_label,
         headers.ipv6.src_addr, headers.ipv6.dst_addr,
         local_metadata.l4_src_port, local_metadata.l4_dst_port},
         HASH_MAX);
  }

  apply {
    select_emcp_hash_algorithm();
    if (headers.ipv4.isValid()) {
      compute_ecmp_hash_ipv4();
    } else if (headers.ipv6.isValid()) {
      compute_ecmp_hash_ipv6();
    }
  }
}  // control hashing

#endif  // SAI_HASHING_P4_

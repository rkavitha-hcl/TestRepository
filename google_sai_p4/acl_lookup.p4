#ifndef SAI_ACL_LOOKUP_P4_
#define SAI_ACL_LOOKUP_P4_

#include <v1model.p4>
#include "headers.p4"
#include "metadata.p4"
#include "ids.h"
#include "resource_limits.p4"

control acl_lookup(inout headers_t headers,
                    inout local_metadata_t local_metadata,
                    inout standard_metadata_t standard_metadata) {
  // First 6 bits of IPv4 TOS or IPv6 traffic class (or 0, for non-IP packets)
  bit<6> dscp = 0;

  @id(ACL_LOOKUP_COUNTER_ID)
  direct_counter(CounterType.packets_and_bytes) acl_lookup_counter;

  @id(ACL_LOOKUP_SET_VRF_ACTION_ID)
  @sai_action(SAI_PACKET_ACTION_FORWARD)
  action set_vrf(@sai_action_param(SAI_ACL_ENTRY_ATTR_EXTENSIONS_ACTION_SET_VRF)
                 @id(1) vrf_id_t vrf_id) {
    local_metadata.vrf_id = vrf_id;
    acl_lookup_counter.count();
  }

  @proto_package("sai")
  @id(ACL_LOOKUP_TABLE_ID)
  @sai_acl(LOOKUP)
  @entry_restriction("
    // Only allow IP field matches for IP packets.
    dscp::mask != 0 -> (is_ip == 1 || is_ipv4 == 1 || is_ipv6 == 1);
    dst_ip::mask != 0 -> is_ipv4 == 1;
    dst_ipv6::mask != 0 -> is_ipv6 == 1;
    // Forbid illegal combinations of IP_TYPE fields.
    is_ip::mask != 0 -> (is_ipv4::mask == 0 && is_ipv6::mask == 0);
    is_ipv4::mask != 0 -> (is_ip::mask == 0 && is_ipv6::mask == 0);
    is_ipv6::mask != 0 -> (is_ip::mask == 0 && is_ipv4::mask == 0);
    // Forbid unsupported combinations of IP_TYPE fields.
    is_ipv4::mask != 0 -> (is_ipv4 == 1);
    is_ipv6::mask != 0 -> (is_ipv6 == 1);
  ")
  table acl_lookup_table {
    key = {
      headers.ipv4.isValid() || headers.ipv6.isValid() : optional @name("is_ip") @id(1)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE/IP);
      headers.ipv4.isValid() : optional @name("is_ipv4") @id(2)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE/IPV4ANY);
      headers.ipv6.isValid() : optional @name("is_ipv6") @id(3)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE/IPV6ANY);
      headers.ethernet.src_addr : ternary @name("src_mac") @id(4)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_SRC_MAC) @format(MAC_ADDRESS);
      headers.ipv4.dst_addr : ternary @name("dst_ip") @id(5)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_DST_IP) @format(IPV4_ADDRESS);
      headers.ipv6.dst_addr : ternary @name("dst_ipv6") @id(6)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6) @format(IPV6_ADDRESS);
      dscp : ternary @name("dscp") @id(7)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_DSCP);
      standard_metadata.ingress_port : optional @name("in_port") @id(8)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_IN_PORT);
    }
    actions = {
      @proto_id(1) set_vrf;
      @defaultonly NoAction;
    }
    const default_action = NoAction;
    counters = acl_lookup_counter;
    size = ACL_LOOKUP_TABLE_SIZE;
  }

  apply {
    if (headers.ipv4.isValid()) {
      dscp = headers.ipv4.dscp;
    } else if (headers.ipv6.isValid()) {
      dscp = headers.ipv6.dscp;
    }

    acl_lookup_table.apply();
  }
}  // control acl_lookup

#endif  // SAI_ACL_LOOKUP_P4_

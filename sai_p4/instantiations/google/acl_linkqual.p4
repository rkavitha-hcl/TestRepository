#ifndef SAI_ACL_LINKQUAL_P4_
#define SAI_ACL_LINKQUAL_P4_

#include <v1model.p4>
#include "../../fixed/headers.p4"
#include "../../fixed/metadata.p4"
#include "ids.h"
#include "resource_limits.p4"

control acl_linkqual(in headers_t headers,
                     inout local_metadata_t local_metadata,
                     inout standard_metadata_t standard_metadata) {

  @id(ACL_LINKQUAL_COUNTER_ID)
  direct_counter(CounterType.packets_and_bytes) linkqual_counter;

  @id(ACL_LINKQUAL_DROP_ACTION_ID)
  @sai_action(SAI_PACKET_ACTION_DROP)
  action linkqual_drop() {
    mark_to_drop(standard_metadata);
    linkqual_counter.count();
  }

  @id(ACL_LINKQUAL_SET_PORT_ACTION_ID)
  @sai_action(SAI_PACKET_ACTION_FORWARD)
  action linkqual_set_port(@sai_action_param(SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT)
                           @id(1) port_id_t port) {
    // Cast is necessary, because v1model does not define port using `type`.
    standard_metadata.egress_spec = (bit<PORT_BITWIDTH>)port;
    linkqual_counter.count();
  }

  @id(ACL_LINKQUAL_TABLE_ID)
  @sai_acl(INGRESS)
  table acl_linkqual_table {
    key = {
      headers.ethernet.ether_type : ternary @name("ether_type") @id(1)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE);
      headers.ethernet.dst_addr : ternary @name("dst_mac") @id(2)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_DST_MAC) @format(MAC_ADDRESS);
      standard_metadata.ingress_port : optional @name("in_port") @id(3)
          @sai_field(SAI_ACL_TABLE_ATTR_FIELD_IN_PORT);
    }
    actions = {
      @proto_id(1) linkqual_drop();
      @proto_id(2) linkqual_set_port();
      @defaultonly NoAction;
    }
    counters = linkqual_counter;
    const default_action = NoAction;
    size = ACL_LINKQUAL_TABLE_SIZE;
  }

  apply {
    acl_linkqual_table.apply();
  }
}  // control acl_linkqual

#endif  // SAI_ACL_LINKQUAL_P4_

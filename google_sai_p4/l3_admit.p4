#ifndef SAI_L3_ADMIT_P4_
#define SAI_L3_ADMIT_P4_

#include <v1model.p4>
#include "headers.p4"
#include "metadata.p4"
#include "ids.h"
#include "resource_limits.p4"

control l3_admit(in headers_t headers,
                 inout local_metadata_t local_metadata,
                 in standard_metadata_t standard_metadata) {
  @id(L3_ADMIT_ACTION_ID)
  action admit_to_l3() {
    local_metadata.admit_to_l3 = true;
  }

  @proto_package("sai")
  @id(L3_ADMIT_TABLE_ID)
  table l3_admit_table {
    key = {
      headers.ethernet.dst_addr : ternary @name("dst_mac") @id(1)
                                          @format(MAC_ADDRESS);
      standard_metadata.ingress_port : optional @name("in_port") @id(2);
    }
    actions = {
      @proto_id(1) admit_to_l3;
      @defaultonly NoAction;
    }
    const default_action = NoAction;
    size = L3_ADMIT_TABLE_SIZE;
  }

  apply {
    l3_admit_table.apply();
  }
}  // control l3_admit

#endif  // SAI_L3_ADMIT_P4_

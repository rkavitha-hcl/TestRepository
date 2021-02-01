#ifndef SAI_MIRRORING_P4_
#define SAI_MIRRORING_P4_

#include <v1model.p4>
#include "headers.p4"
#include "metadata.p4"
#include "ids.h"
#include "resource_limits.p4"

control mirroring(inout headers_t headers,
                  inout local_metadata_t local_metadata,
                  inout standard_metadata_t standard_metadata) {

  // Sets
  // SAI_MIRROR_SESSION_ATTR_TYPE to ENHANCED_REMOTE,
  // SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE to L3_GRE_TUNNEL,
  // SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION to 4,
  // SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE to 0x88BE,
  // SAI_MIRROR_SESSION_ATTR_MONITOR_PORT,
  // SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS,
  // SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS,
  // SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS
  // SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS
  // SAI_MIRROR_SESSION_ATTR_TTL
  // SAI_MIRROR_SESSION_ATTR_TOS
  @id(MIRRORING_MIRROR_AS_IPV4_ERSPAN_ACTION_ID)
  action mirror_as_ipv4_erspan(
      @id(1) port_id_t port,
      @id(2) @format(IPV4_ADDRESS) ipv4_addr_t src_ip,
      @id(3) @format(IPV4_ADDRESS) ipv4_addr_t dst_ip,
      @id(4) @format(MAC_ADDRESS) ethernet_addr_t src_mac,
      @id(5) @format(MAC_ADDRESS) ethernet_addr_t dst_mac,
      @id(6) bit<8> ttl,
      @id(7) bit<8> tos) {
    // Cast is necessary, because v1model does not define port using `type`.
    standard_metadata.egress_spec = (bit<PORT_BITWIDTH>)port;

    // Reference for ERSPAN Type II header construction
    // https://tools.ietf.org/html/draft-foschiano-erspan-00
    headers.erspan_ethernet.setValid();
    headers.erspan_ethernet.src_addr = src_mac;
    headers.erspan_ethernet.dst_addr = dst_mac;
    headers.erspan_ethernet.ether_type = ETHERTYPE_IPV4;

    headers.erspan_ipv4.setValid();
    headers.erspan_ipv4.src_addr = src_ip;
    headers.erspan_ipv4.dst_addr = dst_ip;
    headers.erspan_ipv4.version = 4w4;
    headers.erspan_ipv4.ihl = 4w5;
    headers.erspan_ipv4.protocol = IP_PROTOCOLS_GRE;
    headers.erspan_ipv4.ttl = ttl;
    headers.erspan_ipv4.dscp = tos[7:2];
    headers.erspan_ipv4.ecn = tos[1:0];
    headers.erspan_ipv4.total_len =
      (bit<16>)standard_metadata.packet_length +
      ETHERNET_HEADER_BYTES + IPV4_HEADER_BYTES +
      GRE_HEADER_BYTES + ERSPAN2_HEADER_BYTES;
    // TODO (b/72111753): Investigate if any of these fields should be != 0
    headers.erspan_ipv4.identification = 0;
    headers.erspan_ipv4.flags = 0;
    headers.erspan_ipv4.frag_offset = 0;
    headers.erspan_ipv4.header_checksum = 0;

    headers.erspan_gre.setValid();
    // TODO (b/72111753): Investigate if any of these fields should be != 0
    headers.erspan_gre.checksum_present = 0;
    headers.erspan_gre.routing_present = 0;
    headers.erspan_gre.key_present = 0;
    headers.erspan_gre.sequence_present = 1w1;
    headers.erspan_gre.strict_source_route = 0;
    headers.erspan_gre.recursion_control = 0;
    headers.erspan_gre.acknowledgement_present = 0;
    headers.erspan_gre.flags = 0;
    headers.erspan_gre.version = 0;
    headers.erspan_gre.protocol = GRE_PROTOCOL_ERSPAN;
    // TODO (b/72111753): Increment seq_no per packet via register
    headers.erspan_gre.seq_no = 0;

    headers.erspan_type2.setValid();
    headers.erspan_type2.version = ERSPAN_VERSION_TYPE_II;
    // TODO (b/72111753): Confirm if we should preserve VLAN from original
    headers.erspan_type2.vlan = 0;
    // TODO (b/72111753): Investigate if any of these fields should be != 0
    headers.erspan_type2.cos = 0;
    headers.erspan_type2.trunk_encap = 0;
    headers.erspan_type2.truncate = 0;
    headers.erspan_type2.session_id = 0;
    headers.erspan_type2.reserved = 0;
    headers.erspan_type2.index = 0;

    // TODO: actually implement mirroring the packet.
  }

  @proto_package("sai")
  @id(MIRROR_SESSION_TABLE_ID)
  table mirror_session_table {
    key = {
      local_metadata.mirror_session_id_value : exact @id(1)
                                                     @name("mirror_session_id");
    }
    actions = {
      @proto_id(1) mirror_as_ipv4_erspan;
      @defaultonly NoAction;
    }
    const default_action = NoAction;
    size = MIRROR_SESSION_TABLE_SIZE;
  }

  apply {
    if (local_metadata.mirror_session_id_valid) {
      mirror_session_table.apply();
    }
  }
}  // control mirroring

#endif  // SAI_MIRRORING_P4_

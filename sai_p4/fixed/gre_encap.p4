#ifndef SAI_GRE_ENCAP_P4_
#define SAI_GRE_ENCAP_P4_

#include <v1model.p4>
#include "headers.p4"
#include "metadata.p4"

// To be applied in the egress stage, after the L3 packet rewrite.
control gre_tunnel_encap(inout headers_t headers,
                        in local_metadata_t local_metadata,
                        in standard_metadata_t standard_metadata) {
  apply {
    if (local_metadata.apply_tunnel_encap_at_egress) {
      // Set the gre header fields.
      headers.tunnel_gre.setValid();
      headers.tunnel_gre.checksum_present = 0;
      headers.tunnel_gre.routing_present = 0;
      headers.tunnel_gre.key_present = 0;
      headers.tunnel_gre.sequence_present = 0;
      headers.tunnel_gre.strict_source_route = 0;
      headers.tunnel_gre.recursion_control = 0;
      headers.tunnel_gre.flags = 0;
      headers.tunnel_gre.version = 0;

      // Set the tunnel outer IPV6 header to valid.
      // Currently only outer IPV6 and inner IPV4 or IPV6 is supported.
      headers.tunnel_ipv6.setValid();
      headers.tunnel_ipv6.version = 4w6;

      if (headers.ipv4.isValid()) {
        headers.tunnel_gre.protocol = ETHERTYPE_IPV4;
        // Copy the relevant fields from IPV4 inner to IPV6 tunnel outer header.
        headers.tunnel_ipv6.dscp = headers.ipv4.dscp;
        headers.tunnel_ipv6.ecn = headers.ipv4.ecn;
        headers.tunnel_ipv6.hop_limit = headers.ipv4.ttl;
        headers.tunnel_ipv6.payload_length =  IPV4_HEADER_BYTES +
                                                    GRE_HEADER_BYTES +
                                      (bit<16>)standard_metadata.packet_length;
      }

      if (headers.ipv6.isValid()) {
        headers.tunnel_gre.protocol = ETHERTYPE_IPV6;
        // Copy the inner IPV6 header to IPV6 tunnel outer header.
        headers.tunnel_ipv6.dscp = headers.ipv6.dscp;
        headers.tunnel_ipv6.ecn = headers.ipv6.ecn;
        headers.tunnel_ipv6.hop_limit = headers.ipv6.hop_limit;
        headers.tunnel_ipv6.payload_length = IPV6_HEADER_BYTES +
                                                   GRE_HEADER_BYTES +
                                      (bit<16>)standard_metadata.packet_length;
      }

      // Set the tunnel outer encap IPV6 source & destination address,
      // it is the same when inner is IPV4 or IPV6.
      headers.tunnel_ipv6.src_addr = local_metadata.tunnel_outer_src_ipv6;
      headers.tunnel_ipv6.dst_addr = local_metadata.tunnel_outer_dst_ipv6;
      headers.tunnel_ipv6.next_header = IP_PROTOCOLS_GRE;
    }
  }
}  // gre tunnel encap changes

#endif  // SAI_GRE_ENCAP_P4_

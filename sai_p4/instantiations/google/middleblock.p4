#include <v1model.p4>
#include "../../fixed/headers.p4"
#include "../../fixed/metadata.p4"
#include "../../fixed/parser.p4"
#include "../../fixed/routing.p4"
#include "../../fixed/ipv4_checksum.p4"
#include "../../fixed/mirroring_encap.p4"
#include "../../fixed/mirroring_clone.p4"
#include "../../fixed/l3_admit.p4"
#include "../../fixed/ttl.p4"
#include "../../fixed/packet_rewrites.p4"
#include "acl_ingress.p4"
#include "acl_lookup.p4"
#include "acl_linkqual.p4"

control ingress(inout headers_t headers,
                inout local_metadata_t local_metadata,
                inout standard_metadata_t standard_metadata) {
  apply {
    acl_lookup.apply(headers, local_metadata, standard_metadata);
    l3_admit.apply(headers, local_metadata, standard_metadata);
    routing.apply(headers, local_metadata, standard_metadata);
    acl_linkqual.apply(headers, local_metadata, standard_metadata);
    acl_ingress.apply(headers, local_metadata, standard_metadata);
    ttl.apply(headers, local_metadata, standard_metadata);
    mirroring_clone.apply(headers, local_metadata, standard_metadata);
  }
}  // control ingress

control egress(inout headers_t headers,
               inout local_metadata_t local_metadata,
               inout standard_metadata_t standard_metadata) {
  apply {
    packet_rewrites.apply(headers, local_metadata, standard_metadata);
    mirroring_encap.apply(headers, local_metadata, standard_metadata);
  }
}  // control egress

V1Switch(packet_parser(), verify_ipv4_checksum(), ingress(), egress(),
         compute_ipv4_checksum(), packet_deparser()) main;

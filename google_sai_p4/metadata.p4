#ifndef SAI_METADATA_P4_
#define SAI_METADATA_P4_

#include "ids.h"
#include "headers.p4"
#include "resource_limits.p4"

// -- Translated Types ---------------------------------------------------------

// BMv2 does not support @p4runtime_translation.

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<NEXTHOP_ID_BITWIDTH> nexthop_id_t;

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<WCMP_GROUP_ID_BITWIDTH> wcmp_group_id_t;

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<VRF_BITWIDTH> vrf_id_t;

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<ROUTER_INTERFACE_ID_BITWIDTH> router_interface_id_t;

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<NEIGHBOR_ID_BITWIDTH> neighbor_id_t;

// TODO: This should really be a string, but for the moment we still use
// an 9-bit integer until everything is ready to use strings.
type bit<PORT_BITWIDTH> port_id_t;

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<MIRROR_SESSION_ID_BITWIDTH> mirror_session_id_t;

#ifndef PLATFORM_BMV2
@p4runtime_translation("", string)
#endif
type bit<QOS_QUEUE_BITWIDTH> qos_queue_t;

// -- Meters -------------------------------------------------------------------

enum MeterColor_t { GREEN, YELLOW, RED };

// -- Per Packet State ---------------------------------------------------------

struct headers_t {
  // ERSPAN headers, not extracted during parsing.
  ethernet_t erspan_ethernet;
  ipv4_t erspan_ipv4;
  gre_t erspan_gre;
  erspan2_t erspan_type2;

  ethernet_t ethernet;
  ipv4_t ipv4;
  ipv6_t ipv6;
  icmp_t icmp;
  tcp_t tcp;
  udp_t udp;
  arp_t arp;
}

// Local metadata for each packet being processed.
struct local_metadata_t {
  bool admit_to_l3;
  vrf_id_t vrf_id;
  bit<16> l4_src_port;
  bit<16> l4_dst_port;
  bool mirror_session_id_valid;
  mirror_session_id_t mirror_session_id_value;
  // TODO: consider modeling metering beyond control plane API.
  MeterColor_t color;
}

// -- Packet IO headers --------------------------------------------------------

// TODO: extend the P4 program to actually define the semantics of these.

@controller_header("packet_in")
header packet_in_header_t {
  // The port the packet ingressed on.
  @id(PACKET_IN_INGRESS_PORT_ID)
  port_id_t ingress_port;
  // The initial intended egress port decided for the packet by the pipeline.
  @id(PACKET_IN_TARGET_EGRESS_PORT_ID)
  port_id_t target_egress_port;
}

@controller_header("packet_out")
header packet_out_header_t {
  // The port this packet should egress out of when `submit_to_ingress == 0`.
  // Meaningless when `submit_to_ingress == 1`.
  @id(PACKET_OUT_EGRESS_PORT_ID)
  port_id_t egress_port;
  // Should the packet be submitted to the ingress pipeline instead of being
  // sent directly?
  @id(PACKET_OUT_SUBMIT_TO_INGRESS_ID)
  bit<1> submit_to_ingress;
  // BMV2 backend requires headers to be multiple of 8-bits.
  @id(3)
  bit<7> unused_pad;
}

#endif  // SAI_METADATA_P4_

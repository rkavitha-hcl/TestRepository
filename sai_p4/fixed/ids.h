#ifndef SAI_IDS_H_
#define SAI_IDS_H_

// All declarations (tables, actions, action profiles, meters, counters) have a
// stable ID. This list will evolve as new declarations are added. IDs cannot be
// reused. If a declaration is removed, its ID macro is kept and marked reserved
// to avoid the ID being reused.
//
// The IDs are classified using the 8 most significant bits to be compatible
// with "6.3. ID Allocation for P4Info Objects" in the P4Runtime specification.

// --- Tables ------------------------------------------------------------------

// IDs of fixed SAI tables (8 most significant bits = 0x02).
#define ROUTING_NEIGHBOR_TABLE_ID 0x02000040          // 33554496
#define ROUTING_ROUTER_INTERFACE_TABLE_ID 0x02000041  // 33554497
#define ROUTING_NEXTHOP_TABLE_ID 0x02000042           // 33554498
#define ROUTING_WCMP_GROUP_TABLE_ID 0x02000043        // 33554499
#define ROUTING_IPV4_TABLE_ID 0x02000044              // 33554500
#define ROUTING_IPV6_TABLE_ID 0x02000045              // 33554501
#define MIRROR_SESSION_TABLE_ID 0x02000046            // 33554502
#define L3_ADMIT_TABLE_ID 0x02000047                  // 33554503

// --- Actions -----------------------------------------------------------------

// IDs of fixed SAI actions (8 most significant bits = 0x01).
#define ROUTING_SET_DST_MAC_ACTION_ID 0x01000001              // 16777217
#define ROUTING_SET_PORT_AND_SRC_MAC_ACTION_ID 0x01000002     // 16777218
#define ROUTING_SET_NEXTHOP_ACTION_ID 0x01000003              // 16777219
#define ROUTING_SET_WCMP_GROUP_ID_ACTION_ID 0x01000004        // 16777220
#define ROUTING_SET_NEXTHOP_ID_ACTION_ID 0x01000005           // 16777221
#define ROUTING_DROP_ACTION_ID 0x01000006                     // 16777222
#define MIRRORING_MIRROR_AS_IPV4_ERSPAN_ACTION_ID 0x01000007  // 16777223
#define L3_ADMIT_ACTION_ID 0x01000008                         // 16777224

// --- Copy to CPU session -----------------------------------------------------

// The COPY_TO_CPU_SESSION_ID must be programmed in the target using P4Runtime:
//
// type: INSERT
// entity {
//   packet_replication_engine_entry {
//     clone_session_entry {
//       session_id: COPY_TO_CPU_SESSION_ID
//       replicas { egress_port: 0xfffffffd } # to CPU
//     }
//   }
// }
//
#define COPY_TO_CPU_SESSION_ID 1024

// --- Packet-IO ---------------------------------------------------------------

// Packet-in ingress port field. Indicates which port the packet arrived at.
// Uses @p4runtime_translation(.., string).
#define PACKET_IN_INGRESS_PORT_ID 1

// Packet-in target egress port field. Indicates the port a packet would have
// taken if it had not gotten trapped. Uses @p4runtime_translation(.., string).
#define PACKET_IN_TARGET_EGRESS_PORT_ID 2

// Packet-out egress port field. Indicates the egress port for the packet-out to
// be taken. Mutually exclusive with "submit_to_ingress". Uses
// @p4runtime_translation(.., string).
#define PACKET_OUT_EGRESS_PORT_ID 1

// Packet-out submit_to_ingress field. Indicates that the packet should go
// through the ingress pipeline to determine which port to take (if any).
// Mutually exclusive with "egress_port".
#define PACKET_OUT_SUBMIT_TO_INGRESS_ID 2

#endif  // SAI_IDS_H_

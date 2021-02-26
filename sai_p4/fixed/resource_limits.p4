#ifndef SAI_RESOURCE_LIMITS_P4_
#define SAI_RESOURCE_LIMITS_P4_

// -- Bitwidth definitions -----------------------------------------------------

#ifdef PLATFORM_BMV2

// Number of bits used for types that use @p4runtime_translation("", string).
// This allows BMv2 to support string up to this length.
#define STRING_MAX_BITWIDTH 192 // 24 chars

// TODO: We want to use the commented definition, but BMv2 does not seem
// to support large numbers for ports.
#define PORT_BITWIDTH 9
// #define PORT_BITWIDTH STRING_MAX_BITWIDTH

#define VRF_BITWIDTH STRING_MAX_BITWIDTH
#define NEXTHOP_ID_BITWIDTH STRING_MAX_BITWIDTH
#define NEIGHBOR_ID_BITWIDTH STRING_MAX_BITWIDTH
#define ROUTER_INTERFACE_ID_BITWIDTH STRING_MAX_BITWIDTH
#define WCMP_GROUP_ID_BITWIDTH STRING_MAX_BITWIDTH
#define MIRROR_SESSION_ID_BITWIDTH STRING_MAX_BITWIDTH
#define QOS_QUEUE_BITWIDTH STRING_MAX_BITWIDTH

#else

#define PORT_BITWIDTH 9
#define VRF_BITWIDTH 10
#define NEXTHOP_ID_BITWIDTH 10
#define NEIGHBOR_ID_BITWIDTH 10
#define ROUTER_INTERFACE_ID_BITWIDTH 10
#define WCMP_GROUP_ID_BITWIDTH 12
#define MIRROR_SESSION_ID_BITWIDTH 10
#define QOS_QUEUE_BITWIDTH 8

#endif


// -- Table sizes --------------------------------------------------------------

#define NEXTHOP_TABLE_SIZE 1024

#define NEIGHBOR_TABLE_SIZE 1024

#define ROUTER_INTERFACE_TABLE_SIZE 1024

#define MIRROR_SESSION_TABLE_SIZE 8

#define L3_ADMIT_TABLE_SIZE 512

// The maximum number of wcmp groups.
#define WCMP_GROUP_TABLE_SIZE 4096

// The maximum sum of weights across all wcmp groups.
#define WCMP_GROUP_SELECTOR_MAX_SUM_OF_WEIGHTS_ACROSS_ALL_GROUPS 65536

// The maximum sum of weights for each wcmp group.
#define WCMP_GROUP_SELECTOR_MAX_SUM_OF_WEIGHTS_PER_GROUP 1024

// The selector chooses a group's member, so its bitwidth has to be at least
// log2 of WCMP_GROUP_SELECTOR_MAX_SUM_OF_WEIGHTS_PER_GROUP.
#define WCMP_SELECTOR_INPUT_BITWIDTH 16

#endif  // SAI_RESOURCE_LIMITS_P4_

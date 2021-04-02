#ifndef GOOGLE_SAI_RESOURCE_LIMITS_P4_
#define GOOGLE_SAI_RESOURCE_LIMITS_P4_

// -- Fixed Table sizes --------------------------------------------------------

#define NEXTHOP_TABLE_MINIMUM_GUARANTEED_SIZE 1024

#define NEIGHBOR_TABLE_MINIMUM_GUARANTEED_SIZE 1024

#define ROUTER_INTERFACE_TABLE_MINIMUM_GUARANTEED_SIZE 1024

#define MIRROR_SESSION_TABLE_MINIMUM_GUARANTEED_SIZE 2

#define L3_ADMIT_TABLE_MINIMUM_GUARANTEED_SIZE 512

// The maximum number of wcmp groups.
#define WCMP_GROUP_TABLE_MINIMUM_GUARANTEED_SIZE 4096

// The maximum sum of weights across all wcmp groups.
#define WCMP_GROUP_SELECTOR_MAX_SUM_OF_WEIGHTS_ACROSS_ALL_GROUPS 65536

// The maximum sum of weights for each wcmp group.
#define WCMP_GROUP_SELECTOR_MAX_SUM_OF_WEIGHTS_PER_GROUP 1024

// -- ACL Table sizes ----------------------------------------------------------

#define ACL_INGRESS_TABLE_MINIMUM_GUARANTEED_SIZE 128
#define ACL_LOOKUP_TABLE_MINIMUM_GUARANTEED_SIZE 256

// Maximum channelization for current use-cases is 96 ports, and each port may
// have up to 2 linkqual flows associated with it.
#define ACL_LINKQUAL_TABLE_MINIMUM_GUARANTEED_SIZE 192

// 1 entry for LLDP, 1 entry for ND, and 6 entries for traceroute: TTL 0,1,2 for
// IPv4 and IPv6
#define ACL_WBB_INGRESS_TABLE_MINIMUM_GUARANTEED_SIZE 8

#endif  // GOOGLE_SAI_RESOURCE_LIMITS_P4_

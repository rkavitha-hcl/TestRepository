#ifndef GOOGLE_SAI_RESOURCE_LIMITS_P4_
#define GOOGLE_SAI_RESOURCE_LIMITS_P4_

// -- Table sizes --------------------------------------------------------------

#define ACL_INGRESS_TABLE_SIZE 128
#define ACL_LOOKUP_TABLE_SIZE 256

// Maximum channelization for current use-cases is 96 ports, and each port may
// have up to 2 linkqual flows associated with it.
#define ACL_LINKQUAL_TABLE_SIZE 192

// 1 entry for LLDP, 1 entry for ND, and 6 entries for traceroute: TTL 0,1,2 for
// IPv4 and IPv6
#define ACL_WBB_INGRESS_TABLE_SIZE 8

#endif  // GOOGLE_SAI_RESOURCE_LIMITS_P4_

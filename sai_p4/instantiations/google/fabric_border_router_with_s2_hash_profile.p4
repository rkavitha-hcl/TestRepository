// ECMP value overrrides.
#define ECMP_HASH_OFFSET 8
#define ECMP_HASH_ALGORITHM SAI_HASH_ALGORITHM_CRC_32HI
#define LAG_HASH_OFFSET 7
#define LAG_HASH_ALGORITHM SAI_HASH_ALGORITHM_CRC

#define PKG_INFO_NAME "fabric_border_router_with_s2_hash_profile.p4"

#include "fabric_border_router.p4"

// ECMP value overrrides.
#define ECMP_HASH_OFFSET 0
#define ECMP_HASH_ALGORITHM SAI_HASH_ALGORITHM_CRC_32LO
#define LAG_HASH_OFFSET 7
#define LAG_HASH_ALGORITHM SAI_HASH_ALGORITHM_CRC

#define PKG_INFO_NAME "fabric_border_router_with_s3_hash_profile.p4"

#include "fabric_border_router.p4"

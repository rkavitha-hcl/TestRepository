#ifndef PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_NONSTANDARD_PLATFORMS_H_
#define PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_NONSTANDARD_PLATFORMS_H_

#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/switch_role.h"

namespace sai {

enum class NonstandardPlatform {
  kBmv2,
  kP4Symbolic,
};

// Returns the name of the given platform.
std::string PlatformName(NonstandardPlatform platform);

// Returns JSON config for the SAI P4 program for the given platform.
std::string GetNonstandardP4Config(SwitchRole role,
                                   NonstandardPlatform platform);

// Returns P4 Info for the SAI P4 program for the given platform.
p4::config::v1::P4Info GetNonstandardP4Info(SwitchRole role,
                                            NonstandardPlatform platform);

}  // namespace sai

#endif  // PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_NONSTANDARD_PLATFORMS_H_

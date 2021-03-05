#ifndef PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_P4INFO_H_
#define PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_P4INFO_H_

#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace sai {

enum class SwitchRole {
  kMiddleblock,
  kWbb,
};

// Returns a reference to a static P4info message for the SAI P4 program for the
// given role. The reference is guaranteed to remain valid at all times. If a
// invalid SwitchRole is provided, the method does a LOG(DFATAL) and returns an
// empty P4Info.
const p4::config::v1::P4Info& GetP4Info(SwitchRole role);

// Returns a reference to a static IrP4info message for the SAI P4 program.
// The reference is guaranteed to remain valid at all times.  If a invalid
// SwitchRole is provided, the method does a LOG(DFATAL) and returns an
// empty IrP4Info.
const pdpi::IrP4Info& GetIrP4Info(SwitchRole role);

}  // namespace sai

#endif  // PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_P4INFO_H_

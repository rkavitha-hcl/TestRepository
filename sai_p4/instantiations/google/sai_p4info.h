#ifndef PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_P4INFO_H_
#define PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_P4INFO_H_

#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace sai {

// Returns a reference to a static P4info message for the SAI P4 program.
// The reference is guaranteed to remain valid at all times.
const p4::config::v1::P4Info& GetP4Info();

// Returns a reference to a static IrP4info message for the SAI P4 program.
// The reference is guaranteed to remain valid at all times.
const pdpi::IrP4Info& GetIrP4Info();

}  // namespace sai

#endif  // PLATFORMS_NETWORKING_ORION_P4_SAI_SAI_P4INFO_H_

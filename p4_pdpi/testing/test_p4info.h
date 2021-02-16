#ifndef GOOGLE_P4_PDPI_TESTING_TEST_P4INFO_H_
#define GOOGLE_P4_PDPI_TESTING_TEST_P4INFO_H_

#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace pdpi {

// Returns a reference to a static P4info message for the PDPI test P4 program.
// The reference is guaranteed to remain valid at all times.
const p4::config::v1::P4Info& GetTestP4Info();

// Returns a reference to a static IrP4info message for the PDPI test P4
// program. The reference is guaranteed to remain valid at all times.
const pdpi::IrP4Info& GetTestIrP4Info();

}  // namespace pdpi

#endif  // GOOGLE_P4_PDPI_TESTING_TEST_P4INFO_H_

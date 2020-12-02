// Auxiliary functions for working with PI (p4::v1::*) protobufs.

#ifndef GOOGLE_P4_PDPI_PI_H_
#define GOOGLE_P4_PDPI_PI_H_

#include "absl/status/status.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace pdpi {

// Pads all match field and action parameter values to their full bitwidth,
// as required by some legacy targets such as BMv2.
//
// Note: The resulting table entry is not P4Runtime standard conform.
absl::Status ZeroPadPiTableEntry(const IrP4Info& info,
                                 p4::v1::TableEntry& pi_entry);

}  // namespace pdpi

#endif  // GOOGLE_P4_PDPI_PI_H_

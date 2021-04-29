#ifndef GOOGLE_P4RT_APP_TESTS_LIB_P4RUNTIME_REQUEST_HELPERS_H_
#define GOOGLE_P4RT_APP_TESTS_LIB_P4RUNTIME_REQUEST_HELPERS_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace p4rt_app {
namespace test_lib {

// Translates a PD write request into a PI write request based on the data from
// a pdpi::IrP4Info object.
absl::StatusOr<p4::v1::WriteRequest> PdWriteRequestToPi(
    absl::string_view pd_request, const pdpi::IrP4Info& ir_p4_info);

}  // namespace test_lib
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_TESTS_LIB_P4RUNTIME_REQUEST_HELPERS_H_

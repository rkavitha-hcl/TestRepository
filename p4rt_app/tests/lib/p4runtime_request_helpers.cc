#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"

#include "absl/status/statusor.h"
#include "gutil/proto.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/pd.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"

namespace p4rt_app {
namespace test_lib {

absl::StatusOr<p4::v1::WriteRequest> PdWriteRequestToPi(
    absl::string_view pd_request, const pdpi::IrP4Info& ir_p4_info) {
  p4::v1::WriteRequest request;
  sai::WriteRequest pd_proto;
  RETURN_IF_ERROR(gutil::ReadProtoFromString(pd_request, &pd_proto))
      << "unable to translate PD proto string";
  return pdpi::PdWriteRequestToPi(ir_p4_info, pd_proto);
}

}  // namespace test_lib
}  // namespace p4rt_app

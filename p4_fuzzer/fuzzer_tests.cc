#include "p4_fuzzer/fuzzer_tests.h"

#include <set>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "google_sai_p4/sai_p4info.h"
#include "gutil/status_matchers.h"
#include "p4_fuzzer/annotation_util.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer.pb.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"

ABSL_FLAG(int, fuzzer_iterations, 1000,
          "Number of updates the fuzzer should generate.");

namespace p4_fuzzer {

using ::p4::v1::WriteRequest;

void FuzzP4rtWriteAndCheckNoInternalErrors(thinkit::MirrorTestbed& testbed,
                                           bool mask_known_failures) {
  // Initialize connection.
  thinkit::Switch& sut = testbed.Sut();
  ASSERT_OK_AND_ASSIGN(auto stub, sut.CreateP4RuntimeStub());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> session,
      pdpi::P4RuntimeSession::Create(std::move(stub), sut.DeviceId()));
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(session.get(), sai::GetP4Info()));

  // Clear switch state.
  pdpi::IrP4Info info = sai::GetIrP4Info();
  ASSERT_OK(pdpi::ClearTableEntries(session.get(), info));

  // Run fuzzer.
  int num_updates = 0;
  int num_ok_statuses = 0;
  std::set<std::string> error_messages;
  absl::BitGen gen;
  SwitchState state(info);
  int num_iterations = absl::GetFlag(FLAGS_fuzzer_iterations);
  for (int i = 0; i < num_iterations; i++) {
    if (i % 100 == 0) LOG(INFO) << "Starting iteration " << (i + 1);

    // Generated fuzzed request.
    AnnotatedWriteRequest annotated_request =
        FuzzWriteRequest(&gen, info, state);
    WriteRequest request = RemoveAnnotations(annotated_request);
    num_updates += request.updates_size();

    // Set IDs.
    request.set_device_id(session->DeviceId());
    *request.mutable_election_id() = session->ElectionId();

    ASSERT_OK(testbed.Environment().AppendToTestArtifact(
        "requests_and_responses.txt",
        absl::StrCat("# Write request number ", i + 1, "\n",
                     annotated_request.DebugString())));
    ASSERT_OK(testbed.Environment().AppendToTestArtifact(
        "pi_write_request_trace.txt", request.DebugString()));

    // Send to switch.
    grpc::ClientContext context;
    p4::v1::WriteResponse pi_response;
    ASSERT_OK_AND_ASSIGN(
        pdpi::IrWriteRpcStatus response,
        pdpi::GrpcStatusToIrWriteRpcStatus(
            session->Stub().Write(&context, request, &pi_response),
            request.updates_size()));

    ASSERT_OK(testbed.Environment().AppendToTestArtifact(
        "requests_and_responses.txt",
        absl::StrCat("# Response to request number ", i + 1, "\n",
                     response.DebugString())));

    // TODO: enable this once the switch actually returns a real reply
    // for all inputs.
    if (!mask_known_failures) {
      EXPECT_TRUE(response.has_rpc_response())
          << "Expected proper response, but got: " << response.DebugString();
    }
    if (response.has_rpc_response()) {
      for (const pdpi::IrUpdateStatus& status :
           response.rpc_response().statuses()) {
        // TODO: enable this once the switch stops returning INTERNAL
        // errors.
        if (!mask_known_failures) {
          EXPECT_NE(status.code(), google::rpc::Code::INTERNAL)
              << "Fuzzing should never cause an INTERNAL error, but got: "
              << status.DebugString();
        }
        if (status.code() != google::rpc::Code::OK) {
          error_messages.insert(absl::StrCat(status.code(), status.message()));
        } else {
          num_ok_statuses += 1;
        }
      }
    }
  }

  LOG(INFO) << "Finished " << num_iterations << " iterations.";
  LOG(INFO) << "  num_updates:     " << num_updates;
  LOG(INFO) << "  num_ok_statuses: " << num_ok_statuses;

  ASSERT_OK(testbed.Environment().StoreTestArtifact(
      "error_messages.txt", absl::StrJoin(error_messages, "\n")));

  // Leave the switch in a clean state.
  ASSERT_OK(pdpi::ClearTableEntries(session.get(), info));
}

}  // namespace p4_fuzzer

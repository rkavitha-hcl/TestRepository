#include "p4_fuzzer/fuzzer_tests.h"

#include <set>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "absl/random/seed_sequences.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_fuzzer/annotation_util.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer.pb.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "thinkit/mirror_testbed_fixture.h"
#include "thinkit/test_environment.h"

ABSL_FLAG(int, fuzzer_iterations, 10000,
          "Number of updates the fuzzer should generate.");

namespace p4_fuzzer {

using ::p4::v1::WriteRequest;

TEST_P(FuzzTest, P4rtWriteAndCheckNoInternalErrors) {
  thinkit::Switch& sut = GetMirrorTestbed().Sut();
  thinkit::TestEnvironment& environment = GetMirrorTestbed().Environment();

  bool mask_known_failures = environment.MaskKnownFailures();

  // Initialize connection.
  ASSERT_OK_AND_ASSIGN(auto stub, sut.CreateP4RuntimeStub());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> session,
      pdpi::P4RuntimeSession::Create(std::move(stub), sut.DeviceId()));
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(session.get(), sai::GetP4Info()));

  // Clear switch state.
  pdpi::IrP4Info info = sai::GetIrP4Info();
  ASSERT_OK(pdpi::ClearTableEntries(session.get(), info));

  absl::BitGen gen;

  // Run fuzzer.
  int num_updates = 0;
  int num_ok_statuses = 0;
  int num_notok_without_mutations = 0;
  std::set<std::string> error_messages;
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

    ASSERT_OK(environment.AppendToTestArtifact(
        "requests_and_responses.txt",
        absl::StrCat("# Write request number ", i + 1, "\n",
                     MakeReadable(annotated_request).DebugString())));
    ASSERT_OK(environment.AppendToTestArtifact("pi_write_request_trace.txt",
                                               request.DebugString()));

    // Send to switch.
    grpc::ClientContext context;
    p4::v1::WriteResponse pi_response;
    ASSERT_OK_AND_ASSIGN(
        pdpi::IrWriteRpcStatus response,
        pdpi::GrpcStatusToIrWriteRpcStatus(
            session->Stub().Write(&context, request, &pi_response),
            request.updates_size()));

    ASSERT_OK(environment.AppendToTestArtifact(
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
      for (int i = 0; i < response.rpc_response().statuses().size(); i++) {
        const pdpi::IrUpdateStatus& status =
            response.rpc_response().statuses(i);
        const p4::v1::Update& update = request.updates(i);

        // TODO: enable this once the switch stops returning INTERNAL
        // errors.
        if (!mask_known_failures) {
          EXPECT_NE(status.code(), google::rpc::Code::INTERNAL)
              << "Fuzzing should never cause an INTERNAL error, but got: "
              << status.DebugString();
        }
        // Check resource exhaustion.
        if (status.code() == google::rpc::Code::RESOURCE_EXHAUSTED) {
          int table_id = update.entity().table_entry().table_id();
          ASSERT_OK_AND_ASSIGN(
              const pdpi::IrTableDefinition& table,
              gutil::FindOrStatus(info.tables_by_id(), table_id));
          // TODO: re-enable this check once the switch is fixed.
          if (!(mask_known_failures &&
                table.preamble().alias() == "acl_lookup_table")) {
            // Check that table was full before this status.
            EXPECT_TRUE(state.IsTableFull(table_id))
                << "Switch reported RESOURCE_EXHAUSTED for "
                << table.preamble().alias() << ". The table currently has "
                << state.GetNumTableEntries(table_id)
                << " entries, but is supposed to support at least "
                << table.size() << " entries. Update = " << update.DebugString()
                << "\nState = " << state.SwitchStateSummary();
          }
        }
        // Collect error messages and update state.
        if (status.code() != google::rpc::Code::OK) {
          error_messages.insert(absl::StrCat(
              google::rpc::Code_Name(status.code()), ": ", status.message()));
        } else {
          // TODO: check using ASSERT_OK in the future once the switch
          // no longer fails this.
          state.ApplyUpdate(update).IgnoreError();
          num_ok_statuses += 1;
        }

        bool is_mutated = annotated_request.updates(i).mutations_size() > 0;
        if (status.code() != google::rpc::Code::OK &&
            status.code() != google::rpc::Code::RESOURCE_EXHAUSTED) {
          if (!is_mutated) {
            LOG(WARNING) << "Switch considered update not OK, but fuzzer did "
                            "not use a mutation."
                         << std::endl
                         << "update = "
                         << annotated_request.updates(i).DebugString()
                         << std::endl
                         << "status = " << status.DebugString();
            num_notok_without_mutations += 1;
          }
        }
      }
    }
  }

  LOG(INFO) << "Finished " << num_iterations << " iterations.";
  LOG(INFO) << "  num_updates:                 " << num_updates;
  LOG(INFO) << "  num_ok_statuses:             " << num_ok_statuses;

  // This should be 0 if the fuzzer works correctly
  LOG(INFO) << "  num_notok_without_mutations: " << num_notok_without_mutations;

  LOG(INFO) << "Final state:";
  LOG(INFO) << state.SwitchStateSummary();

  EXPECT_OK(environment.StoreTestArtifact("final_switch_state.txt",
                                          state.SwitchStateSummary()));

  EXPECT_OK(environment.StoreTestArtifact("error_messages.txt",
                                          absl::StrJoin(error_messages, "\n")));

  // Leave the switch in a clean state.
  ASSERT_OK(pdpi::ClearTableEntries(session.get(), info));
}

}  // namespace p4_fuzzer

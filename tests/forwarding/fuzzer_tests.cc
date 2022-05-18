// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "tests/forwarding/fuzzer_tests.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "absl/random/seed_sequences.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_fuzzer/annotation_util.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer.pb.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/sequencing.h"
#include "sai_p4/fixed/roles.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "tests/lib/switch_test_setup_helpers.h"
#include "tests/thinkit_sanity_tests.h"
#include "thinkit/mirror_testbed_fixture.h"
#include "thinkit/test_environment.h"

ABSL_FLAG(int, fuzzer_iterations, 1000,
          "Number of updates the fuzzer should generate.");

// TODO: absl::optional<int> is a much cleaner way to represent
// that there should be no max batch size.
ABSL_FLAG(
    int, fuzzer_max_batch_size, std::numeric_limits<int>::max(),
    "Maximum batch size of a single write request generated by the fuzzer.");

// TODO: In the future, we want to get the default value from the
// GNMI Config (by making this default empty and checking for emptiness in the
// test).
ABSL_FLAG(std::vector<std::string>, fuzzer_ports, {"1"},
          "Set of valid port names for the fuzzer to use.");

// TODO: In the future, we want to get the default value from the
// GNMI Config (by making this default empty and checking for emptiness in the
// test).
ABSL_FLAG(std::vector<std::string>, fuzzer_qos_queues, {"0x1"},
          "Set of valid QoS queues for the fuzzer to use.");

namespace p4_fuzzer {
namespace {

// Buffer time to wind down testing after the test iterations are complete.
constexpr absl::Duration kEndOfTestBuffer = absl::Minutes(5);

// Total time alotted for the test to run.
constexpr absl::Duration kTestTimeout = absl::Hours(1);

bool IsMaskedResource(absl::string_view table_name) {
  // TODO: unmask when acl_egress_table meets resource guarantees.
  return table_name == "acl_egress_table";
}

class TestEnvironment : public testing::Environment {
 public:
  void SetUp() { deadline_ = absl::Now() + kTestTimeout - kEndOfTestBuffer; }
  bool PastDeadline() { return absl::Now() >= deadline_; }

 private:
  absl::Time deadline_;
};

testing::Environment* const env =
    testing::AddGlobalTestEnvironment(new TestEnvironment);

TestEnvironment& Environment() { return *dynamic_cast<TestEnvironment*>(env); }

// TODO: When the oracle handles resource exhaustion, swap out this
// function.
absl::Status ResourceExhaustedIsAllowed(const SwitchState& state,
                                        const pdpi::IrTableDefinition& table,
                                        int max_total_wcmp_members) {
  uint32_t table_id = table.preamble().id();

  // If the table is full, resources may always be exhausted.
  if (state.IsTableFull(table_id)) return absl::OkStatus();

  absl::Status full_status = absl::ResourceExhaustedError(absl::Substitute(
      "Switch reported RESOURCE_EXHAUSTED for table named '$0'. The "
      "table currently has $1 entries, but is supposed to support at "
      "least $2 entries: ",
      table.preamble().alias(), state.GetNumTableEntries(table_id),
      table.size()));

  // If the table uses action profiles, then its resources may be exhausted due
  // to too much total weight or too many total members.
  if (table.implementation_id_case() ==
      pdpi::IrTableDefinition::kActionProfileId) {
    // TODO: Combine these two notions of Action Profile
    // fullness to ActionProfileIsFull, when that is defined.

    // Ensure that total members is above guarantee...
    ASSIGN_OR_RETURN(auto total_actions, state.GetTotalActions(table_id));
    // TODO: Once P4RT standard is updated, derive
    // max_total_wcmp_members from p4info instead. Then, 0 will mean that there
    // is no guarantee placed on total members so we do not use that to
    // determine resource exhaustion. We use the same sentinel value here.
    if (total_actions >= max_total_wcmp_members && max_total_wcmp_members != 0)
      return absl::OkStatus();

    // ... or that total weight is above guarantee.
    absl::Status weight_status =
        state.EnsureActionProfileIsFullOfWeight(table_id);
    if (weight_status.ok()) return absl::OkStatus();
    RETURN_IF_ERROR(full_status)
        << weight_status.message()
        << absl::Substitute(
               "The table currently has $0 total members, but is supposed to "
               "support at least $1.",
               total_actions, max_total_wcmp_members);
  }
  return full_status;
}

}  // namespace

// FuzzerTestFixture class functions

void FuzzerTestFixture::SetUp() {
  GetParam().mirror_testbed->SetUp();
  if (auto& id = GetParam().test_case_id; id.has_value()) {
    GetParam().mirror_testbed->GetMirrorTestbed().Environment().SetTestCaseID(
        *id);
  }
}

void FuzzerTestFixture::TearDown() {
  auto& sut = GetParam().mirror_testbed->GetMirrorTestbed().Sut();

  // Save the logs before reset clearing to help with debug in case of failure.
  if (HasFatalFailure()) {
    EXPECT_OK(GetParam().mirror_testbed->SaveSwitchLogs(
        /*save_prefix=*/"failure_state_"));
  }

  // Attempt to connect to switch and clear tables.
  auto session = pdpi::P4RuntimeSession::Create(sut);
  absl::Status switch_cleared =
      session.ok() ? pdpi::ClearTableEntries(session->get()) : session.status();

  // Though the above statement should never fail, it sometimes
  // inadvertently does due to some bug. Then we reboot the switch to
  // clear the state.
  if (!switch_cleared.ok()) {
    LOG(WARNING)
        << "Failed to clear entries from switch (now attempting reboot): "
        << switch_cleared;
    // Save the logs before rebooting to help with debug.
    EXPECT_OK(GetParam().mirror_testbed->SaveSwitchLogs(
        /*save_prefix=*/"failed_to_clear_sut_state_"));
    pins_test::TestGnoiSystemColdReboot(sut);
  }
  GetParam().mirror_testbed->TearDown();
}

using ::p4::v1::WriteRequest;

TEST_P(FuzzerTestFixture, P4rtWriteAndCheckNoInternalErrors) {
  // Gets the mirror testbed from the test parameters given to
  // FuzzerTestFixture.
  thinkit::MirrorTestbed& mirror_testbed =
      GetParam().mirror_testbed->GetMirrorTestbed();
  thinkit::Switch& sut = mirror_testbed.Sut();
  thinkit::TestEnvironment& environment = mirror_testbed.Environment();

  // Probabilities must be between 0 and 1.
  float mutate_update_probability = GetParam().mutate_update_probability;
  ASSERT_GE(mutate_update_probability, 0.0);
  ASSERT_LE(mutate_update_probability, 1.0);

  ASSERT_OK_AND_ASSIGN(pdpi::IrP4Info info,
                       pdpi::CreateIrP4Info(GetParam().p4info));
  FuzzerConfig config = {
      .info = info,
      .ports = absl::GetFlag(FLAGS_fuzzer_ports),
      .qos_queues = absl::GetFlag(FLAGS_fuzzer_qos_queues),
      .tables_for_which_to_not_exceed_resource_guarantees =
          GetParam().tables_for_which_to_not_exceed_resource_guarantees,
      .disabled_fully_qualified_names =
          GetParam().disabled_fully_qualified_names,
      .role = GetParam().p4rt_role,
      .mutate_update_probability = mutate_update_probability,
  };

  bool mask_known_failures = environment.MaskKnownFailures();

  // Initialize connection, clear switch state, and push the gnmi configuration.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> session,
                       pins_test::ConfigureSwitchAndReturnP4RuntimeSession(
                           sut, GetParam().gnmi_config, GetParam().p4info));

  absl::BitGen gen;

  // Run fuzzer.
  int num_updates = 0;
  int num_ok_statuses = 0;
  int num_notok_without_mutations = 0;
  int num_ok_with_mutations = 0;
  int max_batch_size_seen = 0;
  std::set<std::string> error_messages;
  SwitchState state(info);
  const int max_batch_size = absl::GetFlag(FLAGS_fuzzer_max_batch_size);
  const int num_iterations = absl::GetFlag(FLAGS_fuzzer_iterations);
  for (int i = 0; i < num_iterations; i++) {
    if (Environment().PastDeadline()) {
      ADD_FAILURE() << "Fuzzer test ran out of time after " << i << " out of "
                    << num_iterations << " iterations.";
      break;
    }
    if (i % 100 == 0) LOG(INFO) << "Starting iteration " << (i + 1);

    // Generated fuzzed request.
    AnnotatedWriteRequest annotated_request =
        FuzzWriteRequest(&gen, config, state, max_batch_size);
    WriteRequest request = RemoveAnnotations(annotated_request);
    num_updates += request.updates_size();
    max_batch_size_seen = std::max(max_batch_size_seen, request.updates_size());

    // Set IDs.
    request.set_device_id(session->DeviceId());
    request.set_role(config.role);
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

    // If there is an RPC-wide error, it is unclear what state the switch may be
    // in and we perform a full read to recover it.
    if (!response.has_rpc_response()) {
      // This is technically an error, but we want to focus on other issues
      // during milestone testing and thus are lenient.
      // TODO: The switch currently often returns an RPC-wide error
      // when failing to delete a WCMP group.
      if (!GetParam().milestone.has_value() && !mask_known_failures) {
        ASSERT_TRUE(response.has_rpc_response())
            << "Expected proper response, but got: " << response.DebugString();
      } else {
        LOG(WARNING) << "Attempting to recover switch state after receiving an "
                        "unexpected WriteResponse: "
                     << response.DebugString();
      }

      // Recover switch state via full read.
      ASSERT_OK_AND_ASSIGN(std::vector<p4::v1::TableEntry> table_entries,
                           pdpi::ReadPiTableEntries(session.get()));
      ASSERT_OK(state.SetTableEntries(table_entries));
    }

    // If we get a proper reponse from the switch, we check for resource
    // exhaustion and update our internal view of the switch state
    // incrementally.
    if (response.has_rpc_response()) {
      for (int i = 0; i < response.rpc_response().statuses().size(); i++) {
        const pdpi::IrUpdateStatus& status =
            response.rpc_response().statuses(i);
        const p4::v1::Update& update = request.updates(i);

        EXPECT_NE(status.code(), google::rpc::Code::INTERNAL)
            << "Fuzzing should never cause an INTERNAL error, but got: "
            << status.DebugString();
        // Check resource exhaustion.
        if (status.code() == google::rpc::Code::RESOURCE_EXHAUSTED) {
          int table_id = update.entity().table_entry().table_id();
          ASSERT_OK_AND_ASSIGN(
              const pdpi::IrTableDefinition& table,
              gutil::FindOrStatus(info.tables_by_id(), table_id));

          // Determine if we should check for resource exhaustion:
          // If this is the resource limits test, then we always want to
          // check.
          bool this_is_the_resource_limits_test =
              GetParam().milestone == Milestone::kResourceLimits;

          // If this is not some other labeled test, then we may still want to
          // check...
          bool this_is_not_some_other_specific_test =
              !GetParam().milestone.has_value();

          // ... but we only want to check if we are not masking any failures
          // or just not this specific one.
          bool is_not_masked = !mask_known_failures ||
                               !IsMaskedResource(table.preamble().alias());

          // Mask known failures unless this is the resource limits test.
          // If it is some other specific test, then don't check if resources
          // are exhausted.
          if (this_is_the_resource_limits_test ||
              (this_is_not_some_other_specific_test && is_not_masked)) {
            // Check that table is allowed to have exhausted resources.
            ASSERT_OK(ResourceExhaustedIsAllowed(
                state, table, GetParam().max_total_wcmp_members))
                << "\nUpdate = " << update.DebugString()
                << "\nState = " << state.SwitchStateSummary();
          }
        }
        // Collect error messages and update state.
        if (status.code() != google::rpc::Code::OK) {
          error_messages.insert(absl::StrCat(
              google::rpc::Code_Name(status.code()), ": ", status.message()));
        } else {
          ASSERT_OK(state.ApplyUpdate(update));
          num_ok_statuses += 1;
        }

        bool is_mutated = annotated_request.updates(i).mutations_size() > 0;

        // If the Fuzzer uses a mutation, then the update is likely to be
        // invalid.
        if (status.code() == google::rpc::Code::OK && is_mutated) {
          EXPECT_OK(environment.AppendToTestArtifact(
              "fuzzer_mutated_but_ok.txt",
              absl::StrCat("-------------------\n\nRequest = \n",
                           annotated_request.updates(i).DebugString())));
          num_ok_with_mutations++;
        }

        if (status.code() != google::rpc::Code::OK &&
            status.code() != google::rpc::Code::RESOURCE_EXHAUSTED &&
            status.code() != google::rpc::Code::UNIMPLEMENTED) {
          if (!is_mutated) {
            // Switch did not consider update OK but fuzzer did not use a
            // mutation (i.e. thought the update should be valid).
            EXPECT_OK(environment.AppendToTestArtifact(
                "fuzzer_inaccuracies.txt",
                absl::StrCat("-------------------\n\nrequest = \n",
                             annotated_request.updates(i).DebugString(),
                             "\n\nstatus = \n", status.DebugString())));
            EXPECT_OK(environment.AppendToTestArtifact(
                "fuzzer_inaccuracies_short.txt",
                absl::StrCat(status.message(), "\n")));
            num_notok_without_mutations += 1;
          }
        }
      }
    }

    // Read switch state (to check that reading never fails).
    // TODO: check that the result is the same as switch_state.
    // TODO: do this in every iteration once there is no more performance
    // issue.
    if (i % 25 == 0) {
      ASSERT_OK(pdpi::ReadPiTableEntries(session.get()).status());
    }
  }

  LOG(INFO) << "Finished " << num_iterations << " iterations.";
  LOG(INFO) << "  num_updates:                 " << num_updates;
  // Expected value is 50, so if it's very far from that, we probably have a
  // problem.
  LOG(INFO) << "  Avg updates per request:     "
            << num_updates / static_cast<double>(num_iterations);
  LOG(INFO) << "  max updates in a request:    " << max_batch_size_seen;
  LOG(INFO) << "  num_ok_statuses:             " << num_ok_statuses;

  // These should be 0 if the fuzzer works optimally. These numbers do not
  // affect the soundness of the fuzzer, just the modularity, so it is a goal
  // that we are not 100% strict on as it would be incredibly challenging to
  // enforce. However, it is highly likely that bugs in the switch, which we
  // can't currently detect, are hidden in these numbers.
  LOG(INFO) << "  num_notok_without_mutations: " << num_notok_without_mutations;
  LOG(INFO) << "  num_ok_with_mutations: " << num_ok_with_mutations;

  LOG(INFO) << "Final state:";
  LOG(INFO) << state.SwitchStateSummary();

  EXPECT_OK(environment.StoreTestArtifact("final_switch_state.txt",
                                          state.SwitchStateSummary()));

  EXPECT_OK(environment.StoreTestArtifact("error_messages.txt",
                                          absl::StrJoin(error_messages, "\n")));

  // Unless we are testing a specific milestone, ensure that clearing all
  // tables succeeds. Can be safely skipped as we also clean up the switch
  // during TearDown, but is helpful to detect switch bugs.
  // TODO: The switch currently often returns an RPC-wide error
  // when failing to delete a WCMP group.
  if (!GetParam().milestone.has_value() && !mask_known_failures) {
    ASSERT_OK_AND_ASSIGN(auto table_entries,
                         pdpi::ReadPiTableEntries(session.get()));
    for (const auto& entry : table_entries) {
      EXPECT_OK(environment.AppendToTestArtifact(
          "clearing__pi_entries_read_from_switch.txt", entry));
    }
    std::vector<p4::v1::Update> pi_updates =
        pdpi::CreatePiUpdates(table_entries, p4::v1::Update::DELETE);
    ASSERT_OK_AND_ASSIGN(
        std::vector<p4::v1::WriteRequest> sequenced_clear_requests,
        pdpi::SequencePiUpdatesIntoWriteRequests(info, pi_updates));

    for (int i = 0; i < sequenced_clear_requests.size(); i++) {
      EXPECT_OK(environment.AppendToTestArtifact(
          "clearing__delete_write_requests.txt",
          absl::StrCat("# Delete write batch ", i + 1, ".\n")));
      EXPECT_OK(environment.AppendToTestArtifact(
          "clearing__delete_write_requests.txt", sequenced_clear_requests[i]));
    }
    ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequests(
        session.get(), sequenced_clear_requests));
  }
}

}  // namespace p4_fuzzer

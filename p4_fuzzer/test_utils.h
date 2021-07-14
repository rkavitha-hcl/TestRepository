#ifndef GOOGLE_P4_FUZZER_TEST_UTILS_H_
#define GOOGLE_P4_FUZZER_TEST_UTILS_H_

#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_fuzzer/fuzzer_config.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/ir.pb.h"

namespace p4_fuzzer {

// Options used in the construction of a P4Info by several helper functions.
struct TestP4InfoOptions {
  int action_selector_table_id = 100;
  int action_id = 200;
  int action_no_op_id = 201;
  int action_profile_id = 300;
  int action_profile_size = 65536;
  int action_profile_max_group_size = 256;
};

// Captures the general state shared between most fuzzing functions for use in
// tests.
struct FuzzerTestState {
  absl::BitGen gen;
  FuzzerConfig config;
  SwitchState switch_state;
};

// Constructs an IrP4Info to be used for testing using the options given as
// input.
absl::StatusOr<pdpi::IrP4Info> ConstructIrInfo(
    const TestP4InfoOptions& options);

// Constructs a FuzzerTestState to be used for testing using the options
// given as input.
absl::StatusOr<FuzzerTestState> ConstructFuzzerTestState(
    const TestP4InfoOptions& options);

}  // namespace p4_fuzzer

#endif  // GOOGLE_P4_FUZZER_TEST_UTILS_H_

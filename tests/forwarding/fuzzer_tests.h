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
#ifndef GOOGLE_P4_FUZZER_FUZZER_TESTS_H_
#define GOOGLE_P4_FUZZER_FUZZER_TESTS_H_

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "p4/config/v1/p4info.pb.h"
#include "thinkit/mirror_testbed_fixture.h"

namespace p4_fuzzer {

// Used for testing a specific milestone, ignoring MaskKnownFailures(), rather
// than everything, respecting MaskKnownFailures().
enum class Milestone {
  // Tests that the switch adheres to the minimum guarantees on resources
  // currently defined in
  // google3/third_party/pins_infra/sai_p4/instantiations/google/minimum_guaranteed_sizes.p4.
  kResourceLimits,
  kInvalidInputs,
};

struct FuzzerTestFixtureParams {
  thinkit::MirrorTestbedInterface* mirror_testbed;
  std::string gnmi_config;
  p4::config::v1::P4Info p4info;
  // The probability of performing a mutation on a given table entry.
  float mutate_update_probability = 0.1;
  // Determines which type of issues the fuzzer detects. If left out, the fuzzer
  // will test everything, respecting MaskKnownFailures(). See declaration of
  // Milestone for more details.
  absl::optional<Milestone> milestone;
  absl::optional<std::string> test_case_id;
  // By default, the fuzzer attempts to exceed the listed resource guarantees on
  // all tables, allowing the switch to reject entries beyond those guarantees
  // with a RESOURCE_EXHAUSTED error.
  // This variable lets users specify a set of tables for which the fuzzer
  // should treat their resource guarantees as hard limits rather than trying to
  // go above them. If there are limitations or bugs on the switch causing it to
  // behave incorrectly when the resource guarantees of particular tables are
  // exceeded, this list can be used to allow the fuzzer to produce interesting
  // results in spite of this shortcoming.
  absl::btree_set<std::string>
      tables_for_which_to_not_exceed_resource_guarantees;
  // Fully qualified names of tables, actions, or match fields to skip during
  // fuzzing. Match field names should be prepended with the relevant table name
  // to uniquely identify them.
  // Users should ensure that any set that makes it impossible to generate a
  // valid table entry for a particular table contains the table itself.
  // TODO: Check the property above instead.
  absl::flat_hash_set<std::string> disabled_fully_qualified_names;
  // The P4RT role the fuzzer should use.
  std::string p4rt_role;
  // TODO: Remove once we can derive this number from the P4Info.
  int max_total_wcmp_members;
};

class FuzzerTestFixture
    : public testing::TestWithParam<FuzzerTestFixtureParams> {
 protected:
  // Sets up the mirror test bed, then sets the test_case_id.
  void SetUp() override;

  // Resets switch state after a fatal failure by attempting to clear the switch
  // tables normally, falling back to rebooting the switch. Also runs the
  // standard mirror test bed tear down procedure.
  void TearDown() override;

  ~FuzzerTestFixture() override { delete GetParam().mirror_testbed; }
};

}  // namespace p4_fuzzer

#endif  // GOOGLE_P4_FUZZER_FUZZER_TESTS_H_

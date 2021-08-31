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
};

struct FuzzerTestFixtureParams {
  thinkit::MirrorTestbedInterface* mirror_testbed;
  std::string gnmi_config;
  p4::config::v1::P4Info p4info;
  // Determines which type of issues the fuzzer detects. If left out, the fuzzer
  // will test everything, respecting MaskKnownFailures(). See declaration of
  // Milestone for more details.
  absl::optional<Milestone> milestone;
  absl::optional<std::string> test_case_id;
};

class FuzzerTestFixture
    : public testing::TestWithParam<FuzzerTestFixtureParams> {
 protected:
  void SetUp() override {
    GetParam().mirror_testbed->SetUp();
    if (auto& id = GetParam().test_case_id; id.has_value()) {
      GetParam().mirror_testbed->GetMirrorTestbed().Environment().SetTestCaseID(
          *id);
    }
  }

  void TearDown() override { GetParam().mirror_testbed->TearDown(); }

  ~FuzzerTestFixture() override { delete GetParam().mirror_testbed; }
};

}  // namespace p4_fuzzer

#endif  // GOOGLE_P4_FUZZER_FUZZER_TESTS_H_

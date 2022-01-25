// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GOOGLE_TESTS_FORWARDING_HASHING_TEST_H_
#define GOOGLE_TESTS_FORWARDING_HASHING_TEST_H_

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "p4/config/v1/p4info.pb.h"
#include "thinkit/mirror_testbed_fixture.h"

namespace gpins {

// Holds the common params needed for hashing test.
struct HashingTestParams {
  thinkit::MirrorTestbedInterface* mirror_testbed;
  std::string gnmi_config;
  // TODO: Remove port ids from here and derive from gNMI config.
  std::vector<int> port_ids;
  // Tweak function for rescaling member weights (if applicable) so that the
  // weight used by the tests for statistical calculation matches the hardware
  // (workaround applied) weight.
  absl::optional<std::function<int(int)>> tweak_member_weight;
  // P4Info to be used based on a specific instantiation.
  p4::config::v1::P4Info p4_info;
};

// Test fixture for testing the hashing functionality by verifying the packet
// distribution and the fields used for hashing.
class HashingTestFixture : public testing::TestWithParam<HashingTestParams> {
 protected:
  void SetUp() override { GetParam().mirror_testbed->SetUp(); }
  void TearDown() override { GetParam().mirror_testbed->TearDown(); }
};

}  // namespace gpins

#endif  // GOOGLE_TESTS_FORWARDING_HASHING_TEST_H_

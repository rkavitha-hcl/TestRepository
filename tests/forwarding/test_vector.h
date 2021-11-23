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

#ifndef GOOGLE_TESTS_FORWARDING_TEST_VECTOR_H_
#define GOOGLE_TESTS_FORWARDING_TEST_VECTOR_H_

#include <ostream>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "google/protobuf/descriptor.h"
#include "tests/forwarding/test_vector.pb.h"

namespace gpins {

// Needed to make gUnit produce human-readable output in open source.
inline std::ostream& operator<<(std::ostream& os, const SwitchOutput& output) {
  return os << output.DebugString();
}

// Holds a test vector along with the actual SUT output generated in response to
// the test vector's input. The actual output may be empty, if the switch drops
// the input packet. The test vector may be empty, if the switch generates
// packets that do not correspond to an input, or if the output cannot be
// mapped to a test input.
struct TestVectorAndActualOutput {
  TestVector test_vector;
  SwitchOutput actual_output;
};

// Checks if the `actual_output` conforms to the `test_vector` when ignoring the
// given `ignored_fields`, if any. Returns a failure description in case of a
// mismatch, or `absl::nullopt` otherwise.
absl::optional<std::string> CheckForTestVectorFailure(
    const TestVector& test_vector, const SwitchOutput& actual_output,
    const std::vector<const google::protobuf::FieldDescriptor*>&
        ignored_fields = {});

}  // namespace gpins

#endif  // GOOGLE_TESTS_FORWARDING_TEST_VECTOR_H_

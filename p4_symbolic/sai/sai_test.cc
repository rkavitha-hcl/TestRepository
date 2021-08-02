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
#include "p4_symbolic/sai/sai.h"

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_symbolic/symbolic/symbolic.h"
#include "sai_p4/instantiations/google/instantiations.h"

namespace p4_symbolic {
namespace {

TEST(EvaluateSaiPipeline,
     SatisfiableForAllInstantiationsWithEmptyEntriesAndPorts) {
  std::vector<p4::v1::TableEntry> entries;
  std::vector<int> ports;
  for (auto instantiation : sai::AllInstantiations()) {
    ASSERT_OK_AND_ASSIGN(auto state,
                         EvaluateSaiPipeline(instantiation, entries, ports));
    EXPECT_EQ(state->solver->check(), z3::check_result::sat);
  }
}

}  // namespace
}  // namespace p4_symbolic

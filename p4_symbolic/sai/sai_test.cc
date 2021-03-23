#include "p4_symbolic/sai/sai.h"

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_symbolic/symbolic/symbolic.h"
#include "sai_p4/instantiations/google/switch_role.h"

namespace p4_symbolic {
namespace {

TEST(EvaluateSaiPipeline, SatisfiableForAllRolesWithEmptyEntriesAndPorts) {
  std::vector<p4::v1::TableEntry> entries;
  std::vector<int> ports;
  for (auto role : sai::AllSwitchRoles()) {
    ASSERT_OK_AND_ASSIGN(auto state, EvaluateSaiPipeline(role, entries, ports));
    EXPECT_EQ(state->solver->check(), z3::check_result::sat);
  }
}

}  // namespace
}  // namespace p4_symbolic

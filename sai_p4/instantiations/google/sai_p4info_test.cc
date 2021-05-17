#include "sai_p4/instantiations/google/sai_p4info.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_constraints/backend/constraint_info.h"

namespace sai {
namespace {

// GetP4Info contains a CHECK; ensure it doesn't fail.
TEST(GetP4InfoTest, DoesNotCheckCrashAndP4ConstraintsAreParsable) {
  for (auto instantiation : AllInstantiations()) {
    auto info = GetP4Info(instantiation);
    ASSERT_OK_AND_ASSIGN(p4_constraints::ConstraintInfo constraint_info,
                         p4_constraints::P4ToConstraintInfo(info));
  }
}

// GetIrP4Info contains a CHECK; ensure it doesn't fail.
TEST(GetIrP4InfoTest, DoesNotCheckCrash) {
  for (auto instantiation : AllInstantiations()) {
    auto info = GetIrP4Info(instantiation);
    for (const auto& [name, table] : info.tables_by_name()) {
      EXPECT_NE(table.role(), "")
          << "Table '" << name << "' is missing a @p4runtime_role annotation.";
    }
  }
}

TEST(GetUnionedP4InfoTest, DoesNotCrashTest) { GetUnionedP4Info(); }

}  // namespace
}  // namespace sai

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
  for (auto role : AllSwitchRoles()) {
    auto info = GetP4Info(role);
    ASSERT_OK_AND_ASSIGN(p4_constraints::ConstraintInfo constraint_info,
                         p4_constraints::P4ToConstraintInfo(info));
  }
}

// GetIrP4Info contains a CHECK; ensure it doesn't fail.
TEST(GetIrP4InfoTest, DoesNotCheckCrash) {
  for (auto role : AllSwitchRoles()) {
    GetIrP4Info(role);
  }
}

}  // namespace
}  // namespace sai

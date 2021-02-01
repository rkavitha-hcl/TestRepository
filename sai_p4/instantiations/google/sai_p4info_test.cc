#include "sai_p4/instantiations/google/sai_p4info.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "p4/config/v1/p4info.pb.h"

namespace sai {
namespace {

// GetP4Info contains a CHECK; ensure it doesn't fail.
TEST(GetP4InfoTest, DoesNotCheckCrash) { GetP4Info(); }

// GetIrP4Info contains a CHECK; ensure it doesn't fail.
TEST(GetIrP4InfoTest, DoesNotCheckCrash) { GetIrP4Info(); }

}  // namespace
}  // namespace sai

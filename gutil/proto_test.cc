#include "gutil/proto.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_test.pb.h"
#include "gutil/status_matchers.h"

namespace gutil {
namespace {

using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::IsEmpty;
using ::testing::Not;

TEST(ParseTextProto, EmptyTextProtoIsOk) {
  EXPECT_THAT(ParseTextProto<TestMessage>(""), IsOk());
}

TEST(ParseTextProto, InvalidTextProtoIsNotOk) {
  EXPECT_THAT(ParseTextProto<TestMessage>("bool_field: true"), Not(IsOk()));
}

TEST(ParseTextProto, NonEmptyValidTextProtoIsParsedCorrectly) {
  auto proto = ParseTextProto<TestMessage>(R"pb(
    int_field: 42
    string_field: "hello!"
  )pb");
  ASSERT_THAT(proto, IsOk());
  EXPECT_EQ(proto->int_field(), 42);
  EXPECT_EQ(proto->string_field(), "hello!");
}

TEST(ProtoDiff, ReturnsErrorForIncompatibleMessages) {
  ASSERT_OK_AND_ASSIGN(auto message1, ParseTextProto<TestMessage>(R"pb(
                         int_field: 42
                         string_field: "hello!"
                       )pb"));
  ASSERT_OK_AND_ASSIGN(auto message2, ParseTextProto<AnotherTestMessage>(R"pb(
                         int_field: 42
                         string_field: "hello!"
                       )pb"));
  EXPECT_THAT(ProtoDiff(message1, message2).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ProtoDiff, ReturnsEmptyDiffForEqualMessages) {
  ASSERT_OK_AND_ASSIGN(auto message1, ParseTextProto<TestMessage>(R"pb(
                         int_field: 42
                         string_field: "hello!"
                       )pb"));
  EXPECT_THAT(ProtoDiff(message1, message1), IsOkAndHolds(IsEmpty()));
}

TEST(ProtoDiff, ReturnsNonEmptyDiffForUnequalMessages) {
  ASSERT_OK_AND_ASSIGN(auto message1, ParseTextProto<TestMessage>(R"pb(
                         int_field: 42
                         string_field: "hello!"
                       )pb"));
  ASSERT_OK_AND_ASSIGN(auto message2, ParseTextProto<TestMessage>(R"pb(
                         int_field: 43
                         string_field: "bye"
                       )pb"));
  EXPECT_THAT(ProtoDiff(message1, message2), IsOkAndHolds(Not(IsEmpty())));
}

}  // namespace
}  // namespace gutil

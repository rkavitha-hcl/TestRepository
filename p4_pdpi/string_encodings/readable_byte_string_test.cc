
#include "p4_pdpi/string_encodings/readable_byte_string.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"

namespace pdpi {

using ::gutil::IsOk;
using ::gutil::IsOkAndHolds;
using ::testing::Not;

TEST(ReadableByteStringTest, Positive) {
  // Note: we use PB even though these strings are not protobufs. They are
  // similar enough that the auto-formatter works pretty well though.
  EXPECT_THAT(ReadableByteStringToByteString(R"pb(
                # comments are ignored
                hex: 0x0123
                bin: 0b00000010
                # empty line

                hex: 0x23  # comment at end of line
              )pb"),
              IsOkAndHolds("\x01\x23\x02\x23"));

  EXPECT_THAT(ReadableByteStringToByteString(R"pb(
                # no label
                0x0800
              )pb"),
              IsOkAndHolds(std::string("\x08\x00", 2)));

  EXPECT_THAT(ReadableByteStringToByteString(R"(
                # bin and hex can be mixed across byte boundaries
                some_binary_field: 0b00
                some_hex_field: 0x1
                another_binary_field: 0b00
              )"),
              IsOkAndHolds("\x04"));
}

TEST(ReadableByteStringTest, OnlyFullBytes) {
  EXPECT_THAT(ReadableByteStringToByteString(R"pb(
                bin: 0b00
              )pb"),
              Not(IsOk()));
}

TEST(ReadableByteStringTest, NoInvalidChars) {
  EXPECT_THAT(ReadableByteStringToByteString(R"pb(
                bin: 0b2
              )pb"),
              Not(IsOk()));
  EXPECT_THAT(ReadableByteStringToByteString(R"pb(
                bin: 0xK
              )pb"),
              Not(IsOk()));
}

}  // namespace pdpi


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
  EXPECT_THAT(ReadableByteStringToByteString(R"PB(
                # comments are ignored
                hex: 0x0123
                bin: 0b00000010
                # empty line

                hex: 0x23  # comment at end of line
              )PB"),
              IsOkAndHolds("\x01\x23\x02\x23"));

  EXPECT_THAT(ReadableByteStringToByteString(R"PB(
                # no label
                0x0800
              )PB"),
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
  EXPECT_THAT(ReadableByteStringToByteString(R"PB(
                bin: 0b00
              )PB"),
              Not(IsOk()));
}

TEST(ReadableByteStringTest, NoInvalidChars) {
  EXPECT_THAT(ReadableByteStringToByteString(R"PB(
                bin: 0b2
              )PB"),
              Not(IsOk()));
  EXPECT_THAT(ReadableByteStringToByteString(R"PB(
                bin: 0xK
              )PB"),
              Not(IsOk()));
}

}  // namespace pdpi

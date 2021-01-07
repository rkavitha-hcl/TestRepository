
#include "p4_pdpi/string_encodings/bit_string.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"

namespace pdpi {

using ::gutil::IsOkAndHolds;

// TODO: Consider adding more coverage (though the clients of this
// library have excellent coverage).

TEST(ReadableByteStringTest, OfByteStringWorks) {
  BitString test = BitString::OfByteString("\x01\x2a\xff");

  EXPECT_THAT(test.ToByteString(), IsOkAndHolds("\x01\x2a\xff"));
  EXPECT_THAT(test.ToHexString(), IsOkAndHolds("0x012aff"));
}

TEST(ReadableByteStringTest, ConsumeHexStringWorks) {
  BitString test = BitString::OfByteString("\xff\xff\xff\xff\xff");
  EXPECT_THAT(test.ConsumeHexString(1), IsOkAndHolds("0x1"));
  EXPECT_THAT(test.ConsumeHexString(5), IsOkAndHolds("0x1f"));
  EXPECT_THAT(test.ConsumeHexString(9), IsOkAndHolds("0x1ff"));
}

}  // namespace pdpi

#include "p4_pdpi/string_encodings/byte_string.h"

#include <bitset>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"

namespace pdpi {
namespace {

using ::gutil::IsOk;
using ::gutil::IsOkAndHolds;
using ::testing::Eq;
using ::testing::Not;

std::vector<std::pair<std::bitset<9>, std::string>>
BitsetsAndPaddedByteStrings() {
  return {
      {{0x00'00}, {0x00, 0x00}},
      {{0x00'01}, {0x00, 0x01}},
      {{0x01'cd}, {0x01, 0xcd}},
      {{0x00'23}, {0x00, 0x23}},
  };
}

std::vector<std::pair<std::bitset<9>, std::string>>
BitsetsAndP4RuntimeByteStrings() {
  return {
      {{0x00'00}, {0x00}},
      {{0x00'01}, {0x01}},
      {{0x01'cd}, {0x01, 0xcd}},
      {{0x00'23}, {0x23}},
  };
}

TEST(ByteStringTest, BitsetToPaddedByteStringCorrect) {
  for (auto& [bitset, byte_str] : BitsetsAndPaddedByteStrings()) {
    EXPECT_EQ(BitsetToPaddedByteString(bitset), byte_str);
  }
}

TEST(ByteStringTest, BitsetToP4RuntimeByteStringCorrect) {
  for (auto& [bitset, byte_str] : BitsetsAndP4RuntimeByteStrings()) {
    EXPECT_EQ(BitsetToP4RuntimeByteString(bitset), byte_str);
  }
}

TEST(ByteStringTest, ByteStringToBitsetCorrect) {
  // The empty string is rejected.
  EXPECT_THAT(ByteStringToBitset<9>(""), Not(IsOk()));

  // P4Runtime byte strings are accepted.
  for (auto& [bitset, byte_str] : BitsetsAndP4RuntimeByteStrings()) {
    EXPECT_THAT(ByteStringToBitset<9>(byte_str), IsOkAndHolds(Eq(bitset)));
  }

  // Padded byte strings are accepted.
  for (auto& [bitset, byte_str] : BitsetsAndPaddedByteStrings()) {
    EXPECT_THAT(ByteStringToBitset<9>(byte_str), IsOkAndHolds(Eq(bitset)));

    // Missing bytes are okay -- they will be assumed to be zero.
    EXPECT_THAT(ByteStringToBitset<200>(byte_str),
                IsOkAndHolds(Eq(std::bitset<200>(bitset.to_ulong()))));

    // Extra bytes are also okay if they are zero.
    const std::vector<std::string> zero_prefixes = {{0}, {0, 0}};
    for (auto& prefix : zero_prefixes) {
      EXPECT_THAT(ByteStringToBitset<9>(prefix + byte_str),
                  IsOkAndHolds(Eq(bitset)));
    }

    // Extra bytes are *not* okay if they are non-zero.
    const std::vector<std::string> nonzero_prefixes = {
        {1}, {2}, {3}, {100}, {1, 0},
    };
    for (auto& prefix : nonzero_prefixes) {
      EXPECT_THAT(ByteStringToBitset<9>(prefix + byte_str), Not(IsOk()));
    }
  }

  // Extra nonzero bits are never okay.
  EXPECT_THAT(ByteStringToBitset<1>(std::string{0b01}), IsOk());
  EXPECT_THAT(ByteStringToBitset<1>(std::string{0b10}), Not(IsOk()));
  EXPECT_THAT(ByteStringToBitset<1>(std::string{0, 0b01}), IsOk());
  EXPECT_THAT(ByteStringToBitset<1>(std::string{0, 0b10}), Not(IsOk()));
  EXPECT_THAT(ByteStringToBitset<1>(std::string{0, 0, 0b01}), IsOk());
  EXPECT_THAT(ByteStringToBitset<1>(std::string{0, 0, 0b10}), Not(IsOk()));
  EXPECT_THAT(ByteStringToBitset<2>(std::string{0, 0, 0b010}), IsOk());
  EXPECT_THAT(ByteStringToBitset<2>(std::string{0, 0, 0b100}), Not(IsOk()));
  EXPECT_THAT(ByteStringToBitset<2>(std::string{0, 0, 0b10000}), Not(IsOk()));
}

}  // namespace
}  // namespace pdpi

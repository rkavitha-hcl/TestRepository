#include "p4_pdpi/netaddr/ipv6_address.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/netaddr/network_address.h"
#include "p4_pdpi/string_encodings/safe.h"

namespace netaddr {
namespace {

using ::gutil::IsOk;
using ::gutil::IsOkAndHolds;
using ::pdpi::SafeChar;
using ::testing::Eq;
using ::testing::Not;

// Constructs a byte string from a vector of hextets, padding to 8 hextets
// (16 bytes) on the right to match the size of a padded IPv6 byte string.
std::string Ipv6ByteString(std::vector<uint16_t> hextets) {
  while (hextets.size() < 8) hextets.push_back(0);  // Pad to 8 hextets.
  std::string result;
  for (uint16_t hextet : hextets) {
    result.push_back(SafeChar((hextet >> 8) & 0xFF));
    result.push_back(SafeChar((hextet >> 0) & 0xFF));
  }
  return result;
}

// An IPv6 address, in 3 different representations.
struct IpTriple {
  // IPv6 in canonical notation (https://tools.ietf.org/html/rfc5952#section-4),
  // e.g. "feec:12:1::".
  std::string canonical_notation;
  // Other legal human readable IPv6 strings, e.g. "feec:0012:01::".
  std::vector<std::string> alternative_notations;
  // Padded byte string (big-endian).
  std::string byte_string;
  Ipv6Address ip;
};

std::vector<IpTriple> CorrectIpTriples() {
  std::vector<IpTriple> triples;

  triples.push_back(IpTriple{
      .canonical_notation = "::",
      .alternative_notations = {"0:00:000:0000::", "::0", "0:0:0:0:0:0:0:0"},
      .byte_string = Ipv6ByteString({}),
      .ip = Ipv6Address::AllZeros(),
  });

  triples.push_back(IpTriple{
      .canonical_notation = "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
      .alternative_notations = {"ffff:ffff:ffff:ffff:FFFF:FFFF:FFFF:FFFF"},
      .byte_string = Ipv6ByteString(
          {0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff}),
      .ip = Ipv6Address::AllOnes(),
  });

  triples.push_back(IpTriple{
      .canonical_notation = "123:4567:89ab:cdef::",
      .alternative_notations = {"0123:4567:89ab:cdef::"},
      .byte_string = Ipv6ByteString({0x0123, 0x4567, 0x89ab, 0xcdef}),
      .ip = Ipv6Address(0x0123, 0x4567, 0x89ab, 0xcdef),
  });

  triples.push_back(IpTriple{
      .canonical_notation = "::123:4567:89ab:cdef",
      .alternative_notations = {"0:0:0:0:0123:4567:89ab:cdef"},
      .byte_string =
          Ipv6ByteString({0, 0, 0, 0, 0x0123, 0x4567, 0x89ab, 0xcdef}),
      .ip = Ipv6Address(0, 0, 0, 0, 0x0123, 0x4567, 0x89ab, 0xcdef),
  });

  triples.push_back(IpTriple{
      .canonical_notation = "1::f",
      .alternative_notations = {"01::00f"},
      .byte_string = Ipv6ByteString({0x1, 0, 0, 0, 0, 0, 0, 0xf}),
      .ip = Ipv6Address(0x1, 0, 0, 0, 0, 0, 0, 0xf),
  });

  triples.push_back(IpTriple{
      // Zero-compression is not used for single zeros in canonical notation.
      .canonical_notation = "1:2:3:0:5:6:7:8",
      .alternative_notations = {"1:2:3::5:6:7:8"},
      .byte_string = Ipv6ByteString({0x1, 0x2, 0x3, 0, 0x5, 0x6, 0x7, 0x8}),
      .ip = Ipv6Address(0x1, 0x2, 0x3, 0, 0x5, 0x6, 0x7, 0x8),
  });

  return triples;
}

TEST(Ipv6AddressTest, ConversionsCorrect) {
  for (auto [canonical_notation, alternative_notations, byte_string, ip] :
       CorrectIpTriples()) {
    EXPECT_THAT(ip.ToPaddedByteString(), byte_string);
    EXPECT_THAT(ip.ToString(), canonical_notation);
    EXPECT_THAT(Ipv6Address::OfString(canonical_notation), IsOkAndHolds(Eq(ip)))
        << canonical_notation;
    alternative_notations.push_back(absl::AsciiStrToUpper(canonical_notation));
    for (const auto& notation : alternative_notations) {
      EXPECT_THAT(Ipv6Address::OfString(notation), IsOkAndHolds(Eq(ip)))
          << notation;
    }
  }
}

std::vector<std::string> IncorrectIpStrings() {
  return std::vector<std::string>{
      // Nonsense.
      ":",
      "",
      "192.168.2.1",
      // More than one '::'.
      "a::b::c",
      "1::2::3::4::5::6::7::8",
      // Too many chars in hextet.
      "1:2:3:4:5:6:7:12345",
      // Too short.
      "1",
      "1:2",
      "1:2:3",
      "1:2:3:4",
      "1:2:3:4:5",
      "1:2:3:4:5:6",
      "1:2:3:4:5:6:7",
      // Illegal '::'.
      "1:2:3:4::5:6:7:8",
      // Too long.
      "1:2:3:4:5:6:7:8:9",
      "1:2:3:4:5:6:7:8:9:10",
      "1:2:3:4:5:6:7:8:9:10:11",
      "1:2:3:4:5:6:7:8:9:10:11:12",
  };
}

TEST(Ipv6AddressTest, Ipv6AddressOfString_NegativeTests) {
  for (std::string ip_str : IncorrectIpStrings()) {
    EXPECT_THAT(Ipv6Address::OfString(ip_str), Not(IsOk()))
        << "ip_str = " << ip_str;
  }
}

}  // namespace
}  // namespace netaddr

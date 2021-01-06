#include "p4_pdpi/netaddr/mac_address.h"

#include <bitset>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
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
using ::testing::Eq;
using ::testing::Not;

// An MAC address, in 3 different representations.
struct MacTriple {
  // Cannonical representation
  std::string canonical_notation;
  // Other legal human readable strings, e.g. using uppercase.
  std::vector<std::string> alternative_notations;
  // Padded byte string (big-endian).
  std::string byte_string;
  MacAddress mac;
};

std::vector<MacTriple> CorrectMacTriples() {
  std::vector<MacTriple> triples;

  triples.push_back(MacTriple{
      .canonical_notation = "00:00:00:00:00:00",
      .alternative_notations = {"00:00:00:00:0:0", "0:0:0:0:0:0"},
      .byte_string = pdpi::SafeString({0, 0, 0, 0, 0, 0}),
      .mac = MacAddress::AllZeros(),
  });
  triples.push_back(MacTriple{
      .canonical_notation = "01:23:45:67:89:ab",
      .alternative_notations = {"01:23:45:67:89:ab", "1:23:45:67:89:ab",
                                "01:23:45:67:89:Ab"},
      .byte_string = pdpi::SafeString({0x01, 0x23, 0x45, 0x67, 0x89, 0xab}),
      .mac = MacAddress(0x01, 0x23, 0x45, 0x67, 0x89, 0xab),
  });
  triples.push_back(MacTriple{
      .canonical_notation = "ff:ff:ff:ff:ff:ff",
      .alternative_notations = {"ff:ff:ff:FF:fF:ff", "FF:FF:FF:FF:FF:FF"},
      .byte_string = pdpi::SafeString({0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
      .mac = MacAddress::AllOnes(),
  });

  return triples;
}

TEST(MacAddressTest, ConversionsCorrect) {
  for (auto [canonical_notation, alternative_notations, byte_string, mac] :
       CorrectMacTriples()) {
    EXPECT_THAT(mac.ToPaddedByteString(), byte_string);
    EXPECT_THAT(mac.ToString(), canonical_notation);
    EXPECT_THAT(MacAddress::OfString(canonical_notation), IsOkAndHolds(Eq(mac)))
        << canonical_notation;
    alternative_notations.push_back(absl::AsciiStrToUpper(canonical_notation));
    for (const auto& notation : alternative_notations) {
      EXPECT_THAT(MacAddress::OfString(notation), IsOkAndHolds(Eq(mac)))
          << notation;
    }
  }
}

std::vector<std::string> IncorrectMacStrings() {
  return std::vector<std::string>{
      // Nonsense.
      ":",
      "",
      "192.168.2.1",
      "11:22:33:44:55::66",
      "11:22:33:44::66",
      // Too short.
      "11",
      "11:22",
      "11:22:33",
      "11:22:33:44",
      "11:22:33:44:55",
      // Too long.
      "11:22:33:44:55:66:77",
      "11:22:33:44:55:66:77:88",
      "11:22:33:44:55:66:77:88:99",
  };
}

TEST(MacAddressTest, Ipv6AddressOfString_NegativeTests) {
  for (std::string mac_str : IncorrectMacStrings()) {
    EXPECT_THAT(MacAddress::OfString(mac_str), Not(IsOk()))
        << "mac_str = " << mac_str;
  }
}

}  // namespace
}  // namespace netaddr

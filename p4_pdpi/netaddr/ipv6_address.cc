#include "p4_pdpi/netaddr/ipv6_address.h"

#include <string>
#include <type_traits>

#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "gutil/status.h"
#include "p4_pdpi/netaddr/network_address.h"
#include "p4_pdpi/utils/ir.h"

namespace netaddr {

Ipv6Address::Ipv6Address(uint16_t hextet8, uint16_t hextet7, uint16_t hextet6,
                         uint16_t hextet5, uint16_t hextet4, uint16_t hextet3,
                         uint16_t hextet2, uint16_t hextet1)
    : NetworkAddress{(std::bitset<128>(hextet8) << 112) |
                     (std::bitset<128>(hextet7) << 96) |
                     (std::bitset<128>(hextet6) << 80) |
                     (std::bitset<128>(hextet5) << 64) |
                     (std::bitset<128>(hextet4) << 48) |
                     (std::bitset<128>(hextet3) << 32) |
                     (std::bitset<128>(hextet2) << 16) |
                     (std::bitset<128>(hextet1) << 0)} {};

// TODO: Instead of having this module rely on utils/ir, put the
// implementations here and make the dependency the other way around.

absl::StatusOr<Ipv6Address> Ipv6Address::OfString(absl::string_view address) {
  std::string lower{address.data()};
  absl::AsciiStrToLower(&lower);
  ASSIGN_OR_RETURN(std::string bytes, pdpi::Ipv6ToNormalizedByteString(lower),
                   _.SetPrepend() << "On input '" << address << "': ");
  return Ipv6Address::OfByteString(bytes);
}

std::string Ipv6Address::ToString() const {
  // TODO: Replace this with safe code.
  return pdpi::NormalizedByteStringToIpv6(this->ToPaddedByteString()).value();
}

}  // namespace netaddr

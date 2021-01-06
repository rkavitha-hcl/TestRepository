#ifndef GOOGLE_P4_PDPI_NETADDR_MAC_ADDRESS_H_
#define GOOGLE_P4_PDPI_NETADDR_MAC_ADDRESS_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "p4_pdpi/netaddr/network_address.h"

namespace netaddr {

class MacAddress : public NetworkAddress<48, MacAddress> {
 public:
  // The default constructor returns the address with all bits set to zero.
  constexpr MacAddress() = default;

  // MacAddress(0x01, 0x23, 0x45, 0x67, 0x89, 0xab) constructs the IP address
  // 01:23:45:67:89:ab.
  explicit constexpr MacAddress(uint8_t byte6, uint8_t byte5, uint8_t byte4,
                                uint8_t byte3, uint8_t byte2, uint8_t byte1)
      : NetworkAddress{(static_cast<uint64_t>(byte6) << 40) +
                       (static_cast<uint64_t>(byte5) << 32) +
                       (static_cast<uint64_t>(byte4) << 24) +
                       (static_cast<uint64_t>(byte3) << 16) +
                       (static_cast<uint64_t>(byte2) << 8) +
                       (static_cast<uint64_t>(byte1) << 0)} {};

  // Constructs an MAC address from a string,
  // e.g. "01:23:45:67:89:ab".
  static absl::StatusOr<MacAddress> OfString(absl::string_view address);

  // Returns MAC address in dot-hexadecimal notation, e.g. "01:23:45:67:89:ab".
  std::string ToString() const;

 protected:
  using NetworkAddress::NetworkAddress;
};

}  // namespace netaddr

#endif  // GOOGLE_P4_PDPI_NETADDR_MAC_ADDRESS_H_

#ifndef GOOGLE_P4_PDPI_NETADDR_IPV4_ADDRESS_H_
#define GOOGLE_P4_PDPI_NETADDR_IPV4_ADDRESS_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "p4_pdpi/netaddr/network_address.h"

namespace netaddr {

class Ipv4Address : public NetworkAddress<32, Ipv4Address> {
 public:
  // The default constructor returns the address with all bits set to zero.
  constexpr Ipv4Address() = default;

  // Ipv4Address(192, 168, 2, 1) constructs the IP address 192.168.2.1.
  explicit constexpr Ipv4Address(uint8_t byte4, uint8_t byte3, uint8_t byte2,
                                 uint8_t byte1)
      : NetworkAddress{(static_cast<uint32_t>(byte4) << 24) +
                       (static_cast<uint32_t>(byte3) << 16) +
                       (static_cast<uint32_t>(byte2) << 8) +
                       (static_cast<uint32_t>(byte1) << 0)} {};

  // Constructs an IPv4Address from an IP string in dot-decimal notation,
  // e.g. "192.168.2.1".
  static absl::StatusOr<Ipv4Address> OfString(absl::string_view address);

  // Returns IP address in dot-decimal notation, e.g. "192.168.2.1".
  std::string ToString() const;

 protected:
  using NetworkAddress::NetworkAddress;
};

}  // namespace netaddr

#endif  // GOOGLE_P4_PDPI_NETADDR_IPV4_ADDRESS_H_

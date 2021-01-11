// Bit widths of packet fields. Naming convention is:
//   "k" <header-name> <field-name> "Bitwidth"
// Some headers may be combined, e.g. Ip for IPv4 and IPv6.

#ifndef GOOGLE_P4_PDPI_PACKETLIB_BIT_WIDTHS_H_
#define GOOGLE_P4_PDPI_PACKETLIB_BIT_WIDTHS_H_

namespace packetlib {

// Standard header sizes (for headers without extensions, etc).
constexpr int kStandardEthernetPacketBitwidth = 48 * 2 + 16;
constexpr int kStandardIpv4PacketBitwidth = 160;

// Ethernet constants.
constexpr int kEthernetEthertypeBitwidth = 16;

// IP constants.
constexpr int kIpVersionBitwidth = 4;
constexpr int kIpIhlBitwidth = 4;
constexpr int kIpDscpBitwidth = 6;
constexpr int kIpEcnBitwidth = 2;
constexpr int kIpTotalLengthBitwidth = 16;
constexpr int kIpIdentificationBitwidth = 16;
constexpr int kIpFlagsBitwidth = 3;
constexpr int kIpFragmentOffsetBitwidth = 13;
constexpr int kIpTtlBitwidth = 8;
constexpr int kIpProtocolBitwidth = 8;
constexpr int kIpChecksumBitwidth = 16;

}  // namespace packetlib

#endif  // GOOGLE_P4_PDPI_PACKETLIB_BIT_WIDTHS_H_

// Bit widths of packet fields. Naming convention is:
//   "k" <header-name> <field-name> "Bitwidth"
// Some headers may be combined, e.g. Ip for IPv4 and IPv6.

#ifndef GOOGLE_P4_PDPI_PACKETLIB_BIT_WIDTHS_H_
#define GOOGLE_P4_PDPI_PACKETLIB_BIT_WIDTHS_H_

namespace packetlib {

// Standard header sizes (for headers without extensions, etc).
constexpr int kEthernetHeaderBitwidth = 48 * 2 + 16;
constexpr int kStandardIpv4HeaderBitwidth = 160;
constexpr int kIpv6HeaderBitwidth = 320;
constexpr int kUdpHeaderBitwidth = 64;
constexpr int kTcpHeaderPrefixBitwidth = 32;

// Ethernet constants.
constexpr int kEthernetEthertypeBitwidth = 16;

// IP constants.
constexpr int kIpVersionBitwidth = 4;          // IPv4 & IPv6
constexpr int kIpIhlBitwidth = 4;              // IPv4
constexpr int kIpDscpBitwidth = 6;             // IPv4 & IPv6
constexpr int kIpEcnBitwidth = 2;              // IPv4 & IPv6
constexpr int kIpTotalLengthBitwidth = 16;     // IPv4
constexpr int kIpIdentificationBitwidth = 16;  // IPv4
constexpr int kIpFlagsBitwidth = 3;            // IPv4
constexpr int kIpFragmentOffsetBitwidth = 13;  // IPv4
constexpr int kIpTtlBitwidth = 8;              // IPv4
constexpr int kIpProtocolBitwidth = 8;         // IPv4
constexpr int kIpChecksumBitwidth = 16;        // IPv4
constexpr int kIpFlowLabelBitwidth = 20;       // IPv6
constexpr int kIpPayloadLengthBitwidth = 16;   // IPv6
constexpr int kIpNextHeaderBitwidth = 8;       // IPv6
constexpr int kIpHopLimitBitwidth = 8;         // IPv6

// UDP constants.
constexpr int kUdpPortBitwidth = 16;
constexpr int kUdpLengthBitwidth = 16;
constexpr int kUdpChecksumBitwidth = 16;

// TCP constants.
constexpr int kTcpPortBitwidth = 16;

// Minimum packet sizes, in bytes.
constexpr int kMinNumBytesInEthernetPayload = 46;

}  // namespace packetlib

#endif  // GOOGLE_P4_PDPI_PACKETLIB_BIT_WIDTHS_H_

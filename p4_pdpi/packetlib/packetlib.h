#ifndef GOOGLE_P4_PDPI_PACKETLIB_PACKETLIB_H_
#define GOOGLE_P4_PDPI_PACKETLIB_PACKETLIB_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"

namespace packetlib {

// Parses the given packet. Parsing is a total function, and any aspect that
// cannot be parsed correctly will be put into `payload` of `Packet`.
//
// Even invalid packets will be parsed into the header structure of `Packet`
// when possible. For instance an invalid checksum will be parsed. However, not
// all invalid packets can be parsed into the header structure. Specifically, if
// trying to represent the packet would lose information, the function will
// instead not parse that header and put the data in `payload` instead. For
// example IPv4 packets with options is treated this way (since the library does
// not support options and thus has no place in `Ipv4Header` for options).
//
// Parsing starts with the given header (defaulting to Ethernet).
//
// Guarantees for `packet = ParsePacket(data)`:
// 1. Valid packets are valid:
//    `packet.reasons_invalid.empty()` implies `ValidatePacket(packet).ok()`.
// 2. Parsing is loss-less:
//    Running `serialized = RawSerializePacket(packet);` guarantees
//    `serialized.ok() && *serialized == data`.
// 3. If a header is supported by packetlib, it will be parsed. Partially
//    supported headers may not be parsed, but then `reason_unsupported`
//    will indicate what unsupported feature the packet uses, and the
//    unsupported header will appear uninterpreted in the payload.
Packet ParsePacket(absl::string_view input,
                   Header::HeaderCase first_header = Header::kEthernetHeader);

// Validates packets by checking that:
// 1. Headers appear in a valid order, and that fields indicating the next
//    header match the actual next header for all supported header types.
// 2. Each field is specified, and of the correct format.
// 3. That computed fields have the right value.
// 4. The packet has the required minimum size.
absl::Status ValidatePacket(const Packet& packet);

// Same as ValidatePacket, but returns a list of reasons why the packet isn't
// valid instead.
std::vector<std::string> PacketInvalidReasons(const Packet& packet);

// Seralizes a given packet. The packet may miss computed fields, which will be
// filled in automatically when missing (but not changed if they are present).
// Serialization succeeds iff `ValidatePacket(packet).ok()` after
// calling `UpdateComputedFields(packet)`; an error status is returned
// otherwise.
absl::StatusOr<std::string> SerializePacket(Packet packet);

// Seralizes a given packet without checking header invariants. All fields must
// be present and use a valid value (according to ir::Format), but otherwise no
// requirements are made on the set of headers; they will just be serialized in
// order without checking, if computed fields are correct, header order is
// valid, etc.
absl::StatusOr<std::string> RawSerializePacket(const Packet& packet);

// Updates all computed fields that are missing. Computed fields that are
// already present are not modified. Returns true iff any changes where made.
// Fails if fields that are required for determining computed fields are missing
// or invalid.
absl::StatusOr<bool> UpdateComputedFields(Packet& packet);

// Returns the size of the given packet in bits, starting at the nth header and
// ignoring all headers before that. Works even when computed fields are
// missing.
// Returns error if
// - `start_header_index` is not in [0, packet.headers().size()], or
// - `packet.headers(i)` is uninitialized for i in
//   [start_header_index, packet.headers().size()]
absl::StatusOr<int> PacketSizeInBits(const Packet& packet,
                                     int start_header_index = 0);

// Like `PacketSizeInBits`, but returns size in bytes, or an error if the bit
// size is not divisible by 8.
absl::StatusOr<int> PacketSizeInBytes(const Packet& packet,
                                      int start_header_index = 0);

// Computes the checksum of an IPv4 header. All fields must be set and valid
// except possibly the checksum, which is ignored.
absl::StatusOr<uint16_t> Ipv4HeaderChecksum(const Ipv4Header& header);

std::string HeaderCaseName(Header::HeaderCase header_case);

}  // namespace packetlib

#endif  // GOOGLE_P4_PDPI_PACKETLIB_PACKETLIB_H_

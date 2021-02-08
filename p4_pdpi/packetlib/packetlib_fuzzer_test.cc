
#include <sstream>
#include <string>
#include <utility>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "p4_pdpi/string_encodings/readable_byte_string.h"

namespace packetlib {

using ::gutil::IsOkAndHolds;

constexpr int kFuzzerIterations = 10000;

// We use templates to help the fuzzer come up with parsable packets, otherwise
// one usually can only parse the first header. Templates are readable bit
// strings, but with ? for random hex characters.
constexpr char kIpv4PacketTemplate[] = R"R(
  # ethernet header
  ethernet_source: 0x????????????
  ethernet_destination: 0x????????????
  ether_type : 0x0800
)R";

std::string RandomPacket(absl::BitGen& gen,
                         const std::string& packet_template = "") {
  int num_bytes = absl::Uniform<int>(gen, /*lo=*/1, /*hi=*/200);
  std::string result;

  if (!packet_template.empty()) {
    std::string readable_byte_string;
    // Substitute '?' with a random hex character.
    for (char c : packet_template) {
      if (c == '?') {
        readable_byte_string += pdpi::HexDigitToChar(absl::Uniform(gen, 0, 16));
      } else {
        readable_byte_string += c;
      }
    }
    auto prefix = pdpi::ReadableByteStringToByteString(readable_byte_string);
    CHECK(prefix.status().ok())
        << "Invalid packet template: " << packet_template;
    result = *prefix;
  }

  // Randomly fill the rest of the packet.
  for (int i = result.size(); i < num_bytes; i++) {
    char c = absl::Uniform<int>(gen, /*lo=*/0, /*hi=*/256);
    result += c;
  }
  return result;
}

std::string ShortPacketDescription(const Packet& packet) {
  std::stringstream result;
  for (const Header& header : packet.headers()) {
    auto header_name = gutil::GetOneOfFieldName(header, "header");
    result << header_name.value_or("error") << "; ";
  }
  int size = packet.payload().size();
  result << size << " byte" << (size > 0 ? "s" : "") << " payload";
  if (!packet.reasons_invalid().empty()) {
    result << "; invalid";
  }
  if (!packet.reason_unsupported().empty()) {
    result << "; unsupported";
  }
  return result.str();
}

class PacketlibFuzzerTest
    : public testing::TestWithParam<std::pair<std::string, std::string>> {};

TEST_P(PacketlibFuzzerTest, RandomPacketStringParseAndSerializeRoundtrip) {
  absl::BitGen gen;

  std::string packet_template = GetParam().second;

  for (int i = 0; i < kFuzzerIterations; i++) {
    std::string packet = RandomPacket(gen, packet_template);
    Packet parsed_packet = ParsePacket(packet);

    LOG(INFO) << "Fuzzing packet: " << ShortPacketDescription(parsed_packet);

    std::string context =
        absl::StrCat("\npacket = ", absl::BytesToHexString(packet),
                     "\nparsed_packet = ", parsed_packet.DebugString());

    absl::StatusOr<std::string> serialized_packet =
        RawSerializePacket(parsed_packet);
    if (serialized_packet.ok()) {
      EXPECT_EQ(absl::BytesToHexString(packet),
                absl::BytesToHexString(*serialized_packet))
          << context;
      EXPECT_THAT(PacketSizeInBits(parsed_packet),
                  IsOkAndHolds(packet.size() * 8))
          << context;

      if (parsed_packet.reasons_invalid().empty()) {
        EXPECT_OK(ValidatePacket(parsed_packet))
            << "parsed_packet =\n"
            << parsed_packet.DebugString() << "\n"
            << "\nValidatePacket(parsed_packet) = "
            << ValidatePacket(parsed_packet).ToString();
      }
    } else {
      ADD_FAILURE() << "Parsed random packet string and tried to serialize, "
                       "but failed\nerror = "
                    << serialized_packet.status() << context;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Templates, PacketlibFuzzerTest,
    testing::ValuesIn(std::vector<std::pair<std::string, std::string>>(
        {{"random", ""}, {"IPv4", kIpv4PacketTemplate}})),
    [](const testing::TestParamInfo<PacketlibFuzzerTest::ParamType>& info) {
      return info.param.first;
    });

}  // namespace packetlib

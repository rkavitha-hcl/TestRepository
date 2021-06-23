// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tests/forwarding/hashing_test.h"

#include <net/ethernet.h>
#include <netinet/in.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <ostream>
#include <vector>

#include "absl/algorithm/algorithm.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "boost/math/distributions/chi_squared.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "p4_pdpi/netaddr/mac_address.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/pd.h"
#include "p4_pdpi/string_encodings/decimal_string.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/test_vector.h"
#include "tests/forwarding/util.h"

// Test for the hashing behavior of the switch.  See go/p4-hashing for a
// description of the design.

// If you run this test and want to convince yourself that it is doing the right
// thing, you can inspect the test log (which will output distributions and
// p-values for all configurations).  You can also inspect all packets that are
// being sent by looking at the test output files.  Finally, the function
// PacketsShouldBeHashed specifies which test configuration are expected to be
// load-balanced and which are epxected to be forwarded on a single port.

// TODO switch generates router solicitation packets.
ABSL_FLAG(bool, ignore_router_solicitation_packets, true,
          "Ignore router solicitation packets.");
// TODO: IPV4_SRC_PORT & L4_DST_PORT field hashing distribution is
// not working.
ABSL_FLAG(bool, ignore_l4_port_hashing, true,
          "Ignore known failure for L4_SRC_PORT & L4_DST_PORT field hashing.");

namespace gpins {
namespace {

using PacketlibPacket = ::packetlib::Packet;
using ::packetlib::EthernetHeader;
using ::packetlib::IpDscp;
using ::packetlib::IpEcn;
using ::packetlib::IpFlags;
using ::packetlib::IpFlowLabel;
using ::packetlib::IpFragmentOffset;
using ::packetlib::IpHopLimit;
using ::packetlib::IpIdentification;
using ::packetlib::IpIhl;
using ::packetlib::IpNextHeader;
using ::packetlib::IpProtocol;
using ::packetlib::IpTtl;
using ::packetlib::Ipv4Header;
using ::packetlib::Ipv6Header;
using ::packetlib::UdpHeader;
using ::packetlib::UdpPort;

constexpr absl::Duration kDurationToWaitForPacketsFromSut = absl::Seconds(30);

struct Member {
  uint32_t weight;
  int port;
};

enum PacketField {
  ETH_SRC,
  ETH_DST,
  IP_SRC,
  IP_DST,
  HOP_LIMIT,
  DSCP,
  FLOW_LABEL_LOWER_16,
  FLOW_LABEL_UPPER_4,
  INNER_IP_SRC,
  INNER_IP_DST,
  INNER_HOP_LIMIT,
  INNER_DSCP,
  INNER_FLOW_LABEL_LOWER_16,
  INNER_FLOW_LABEL_UPPER_4,
  L4_SRC_PORT,
  L4_DST_PORT,
  INPUT_PORT,
};

std::vector<PacketField> AllFields() {
  return {
      ETH_SRC,
      ETH_DST,
      IP_SRC,
      IP_DST,
      HOP_LIMIT,
      DSCP,
      FLOW_LABEL_LOWER_16,
      FLOW_LABEL_UPPER_4,
      INNER_IP_SRC,
      INNER_IP_DST,
      INNER_HOP_LIMIT,
      INNER_DSCP,
      INNER_FLOW_LABEL_LOWER_16,
      INNER_FLOW_LABEL_UPPER_4,
      L4_SRC_PORT,
      L4_DST_PORT,
      INPUT_PORT,
  };
}

std::string PacketFieldToString(const PacketField field) {
  switch (field) {
    case ETH_SRC:
      return "ETH_SRC";
    case ETH_DST:
      return "ETH_DST";
    case IP_SRC:
      return "IP_SRC";
    case IP_DST:
      return "IP_DST";
    case HOP_LIMIT:
      return "HOP_LIMIT";
    case DSCP:
      return "DSCP";
    case FLOW_LABEL_LOWER_16:
      return "FLOW_LABEL_LOWER_16";
    case FLOW_LABEL_UPPER_4:
      return "FLOW_LABEL_UPPER_4";
    case INNER_IP_SRC:
      return "INNER_IP_SRC";
    case INNER_IP_DST:
      return "INNER_IP_DST";
    case INNER_HOP_LIMIT:
      return "INNER_HOP_LIMIT";
    case INNER_DSCP:
      return "INNER_DSCP";
    case INNER_FLOW_LABEL_LOWER_16:
      return "INNER_FLOW_LABEL_LOWER_16";
    case INNER_FLOW_LABEL_UPPER_4:
      return "INNER_FLOW_LABEL_UPPER_4";
    case L4_SRC_PORT:
      return "L4_SRC_PORT";
    case L4_DST_PORT:
      return "L4_DST_PORT";
    case INPUT_PORT:
      return "INPUT_PORT";
  }
  return "";
}

// A description of a single hash test, specifying what kind of packets we want
// to send (IPv4 vs IPv6, etc), and which field we are going to vary.
struct TestConfiguration {
  // The field to be modified.
  PacketField field;
  // (outer) IPv4 or IPv6?
  bool ipv4;
  // Is this packet encapped?
  bool encapped;
  // Inner/encapped header is IPv4 or IPv6?
  bool inner_ipv4;
  // Will this packet be decapped?
  bool decap;
};
struct TestInputOutput {
  TestConfiguration config;
  // The list of packets received
  std::vector<Packet> output;
};
struct TestData {
  absl::Mutex mutex;
  int total_packets_received = 0;
  int total_invalid_packets_received = 0;
  absl::flat_hash_map<std::string, TestInputOutput> data ABSL_GUARDED_BY(mutex);
};

// Returns true if packets generated for this config should be load-balanced.
bool PacketsShouldBeHashed(const TestConfiguration& config) {
  switch (config.field) {
    case IP_SRC:
    case IP_DST:
    case FLOW_LABEL_LOWER_16:
      return !config.encapped;
    case INNER_IP_SRC:
    case INNER_IP_DST:
    case INNER_FLOW_LABEL_LOWER_16:
    case L4_SRC_PORT:
    case L4_DST_PORT:
      return true;
    default:
      return false;
  }
}

// Is this a inner IP field?
bool IsInnerIpField(const PacketField field) {
  switch (field) {
    case INNER_IP_SRC:
    case INNER_IP_DST:
    case INNER_HOP_LIMIT:
    case INNER_DSCP:
    case INNER_FLOW_LABEL_LOWER_16:
    case INNER_FLOW_LABEL_UPPER_4:
      return true;
    default:
      return false;
  }
}

// Is this a valid test configuration?  Not all configurations are valid, e.g.
// you can't modify the flow label in an IPv4 packet (because there is no flow
// label there).
bool IsValidTestConfiguration(const TestConfiguration& config) {
  // FLOW_LABEL only exists for IPv6
  if (config.field == FLOW_LABEL_LOWER_16 && config.ipv4) return false;
  if (config.field == FLOW_LABEL_UPPER_4 && config.ipv4) return false;
  if (config.field == INNER_FLOW_LABEL_LOWER_16 && config.inner_ipv4)
    return false;
  if (config.field == INNER_FLOW_LABEL_UPPER_4 && config.inner_ipv4)
    return false;
  // If the packet is not encapped, various things don't make sense
  if (!config.encapped) {
    // Can only decap an encapped packet
    if (config.decap) return false;
    // inner_ipv4 is ignored for non-encapped packets, so only use one of the
    // two values.
    if (config.inner_ipv4) return false;
    // Cannot vary inner fields if not encapped.
    if (IsInnerIpField(config.field)) return false;
  }
  // encapped traffic with v6 outer is not currently a use-case, so we are not
  // testing it.
  if (config.encapped && !config.ipv4) return false;
  return true;
}

// Number of Wcmp members in a group for this test.
constexpr int kNumWcmpMembersForTest = 3;

constexpr absl::string_view kSetVrfTableEntry = R"pb(
  acl_lookup_table_entry {
    match {}
    action { set_vrf { vrf_id: "vrf-80" } }
    priority: 1129
  })pb";

constexpr absl::string_view kIpv4DefaultRouteEntry = R"pb(
  ipv4_table_entry {
    match { vrf_id: "vrf-80" }
    action { set_wcmp_group_id { wcmp_group_id: "41" } }
  }
)pb";

constexpr absl::string_view kIpv6DefaultRouteEntry = R"pb(
  ipv6_table_entry {
    match { vrf_id: "vrf-80" }
    action { set_wcmp_group_id { wcmp_group_id: "41" } }
  })pb";

constexpr absl::string_view kRouterInterfaceTemplate = R"pb(
  router_interface_table_entry {
    match { router_interface_id: "" }
    action { set_port_and_src_mac { port: "" src_mac: "1" } }
  })pb";

constexpr absl::string_view kNeighborEntryTemplate = R"pb(
  neighbor_table_entry {
    match { router_interface_id: "" neighbor_id: "" }
    action { set_dst_mac { dst_mac: "3c:28:6d:34:c0:02" } }
  })pb";

constexpr absl::string_view kNextHopTemplate = R"pb(
  nexthop_table_entry {
    match { nexthop_id: "" }
    action { set_nexthop { router_interface_id: "" neighbor_id: "" } }
  })pb";

constexpr absl::string_view kMemberEntryTemplate = R"pb(
  action { set_nexthop_id { nexthop_id: "" } }
  weight: 1
  watch_port: "13"
)pb";

constexpr absl::string_view kGroupEntryTemplate = R"pb(
  wcmp_group_table_entry { match { wcmp_group_id: "41" } })pb";

constexpr absl::string_view kDstMacClassifier = R"pb(
  l3_admit_table_entry {
    match {}
    action { admit_to_l3 {} }
    priority: 2070
  })pb";

const uint64_t kBaseDstMac = 234;
const uint64_t kMaxMacAddr = static_cast<uint64_t>(1) << (6 * 8);

// Base IPv4 address for generating the outer IP header for packets that are not
// supposed to be decapped.
const uint32_t kBaseIpV4Src = 0x01020304;
// const char kBaseIpV4Src[] = "1.2.3.4";
const uint32_t kBaseIpV4Dst = 0x02030405;
// const char kBaseIpV4Dst[] = "2.3.4.5";
//  Same, but for packets that are supposed to be decapped.  There is a decap
//  flow for the 10 IPs immediately following these IPs.  10 is enough because
//  packets that vary the IPv4 src/dst and get decapped must not be hashed (the
//  inner IP address should be used).
const uint32_t kBaseDecapIpV4Src = 0x0a020304;  // 10.2.3.4
// const char kBaseDecapIpV4Src[] = "10.2.3.4";
const uint32_t kBaseDecapIpV4Dst = 0x14030405;  // 20.3.4.5
// const char kBaseDecapIpV4Dst[] = "20.3.4.5";

// Number of extra packets to send.  Up to this many packets can then be dropped
// and we can still perform the statistical test.
constexpr int kNumExtraPackets = 10;

// Returns the number of packets to send.
int GetNumPackets(bool should_be_hashed) {
  // Current max packets is set for a max sum of weights 15, error rate of 10%
  // and pvalue of 0.001.
  if (should_be_hashed) return 7586;
  return 10;
}
int GetNumPackets(TestConfiguration config) {
  return GetNumPackets(PacketsShouldBeHashed(config));
}

// Returns the ith destination MAC that is used when varying that field.
uint64_t GetIthDstMac(int i) { return kBaseDstMac + i % kMaxMacAddr; }

// L4 ports we need to avoid (otherwise the marconi packet parsing library will
// try to parse beyond the UDP header and screw up our payload)
const uint32_t kAvoidedL4Ports[] = {2152, 4754};

uint32_t kMaxPorts = 1 << 16;

// Returns the ith L4 port, given a base port
uint32_t GetIthL4Port(int i, uint32_t base) {
  uint32_t result = base + i % kMaxPorts;
  for (uint32_t avoid : kAvoidedL4Ports) {
    if (result >= avoid) result += 1;
  }
  return result;
}

absl::Status SetUpSut(pdpi::P4RuntimeSession* p4_session) {
  RETURN_IF_ERROR(
      pdpi::SetForwardingPipelineConfig(
          p4_session,
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          sai::GetP4Info(sai::Instantiation::kMiddleblock)))
          .SetPrepend()
      << "Failed to push P4Info for Sut: ";
  RETURN_IF_ERROR(pdpi::ClearTableEntries(
      p4_session, sai::GetIrP4Info(sai::Instantiation::kMiddleblock)));
  return absl::OkStatus();
}

absl::Status SetUpControlSwitch(pdpi::P4RuntimeSession* p4_session) {
  RETURN_IF_ERROR(
      pdpi::SetForwardingPipelineConfig(
          p4_session,
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          sai::GetP4Info(sai::Instantiation::kMiddleblock)))
          .SetPrepend()
      << "Failed to push P4Info for Control switch: ";
  RETURN_IF_ERROR(pdpi::ClearTableEntries(
      p4_session, sai::GetIrP4Info(sai::Instantiation::kMiddleblock)));
  // Trap all packets on control switch.
  ASSIGN_OR_RETURN(
      p4::v1::TableEntry punt_all_pi_entry,
      pdpi::PdTableEntryToPi(
          sai::GetIrP4Info(sai::Instantiation::kMiddleblock),
          gutil::ParseProtoOrDie<sai::TableEntry>(
              R"pb(
                acl_ingress_table_entry {
                  match {}                              # Wildcard match.
                  action { trap { qos_queue: "0x1" } }  # Action: punt.
                  priority: 1                           # Highest priority.
                }
              )pb")));
  return pdpi::InstallPiTableEntry(p4_session, punt_all_pi_entry);
}

absl::Status SaveProto(thinkit::MirrorTestbed& testbed,
                       const google::protobuf::Message& message,
                       absl::string_view path) {
  return testbed.Environment().StoreTestArtifact(path, message.DebugString());
}

absl::Status AppendProto(thinkit::MirrorTestbed& testbed,
                         const google::protobuf::Message& message,
                         absl::string_view path) {
  return testbed.Environment().AppendToTestArtifact(
      path, absl::StrCat(message.DebugString(), "\n"));
}

// Convery Pd table entry to Pi update request.
absl::StatusOr<p4::v1::Update> PdTableEntryToPiUpdate(
    const sai::TableEntry& pd_entry) {
  ASSIGN_OR_RETURN(
      auto pi_entry,
      pdpi::PdTableEntryToPi(sai::GetIrP4Info(sai::Instantiation::kMiddleblock),
                             pd_entry),
      _.SetPrepend() << "Failed in PD table conversion to PI, entry: "
                     << pd_entry.DebugString() << " error: ");
  p4::v1::Update update;
  update.set_type(p4::v1::Update::INSERT);
  *update.mutable_entity()->mutable_table_entry() = pi_entry;
  EXPECT_OK(AppendProto(thinkit::MirrorTestbedFixture::GetParam()
                            .mirror_testbed->GetMirrorTestbed(),
                        pd_entry, "flows.pd.txt"));
  return update;
}

// Returns the set of entities required for the hasing test.
absl::StatusOr<p4::v1::WriteRequest> GetHashingEntities(
    absl::Span<const Member> members) {
  p4::v1::WriteRequest result;

  // Set default VRF for all packets.
  ASSIGN_OR_RETURN(
      *result.add_updates(),
      PdTableEntryToPiUpdate(
          gutil::ParseProtoOrDie<sai::TableEntry>(kSetVrfTableEntry)));

  int index = 0;
  // Create router interface, neighbor and nexthop entry for every member.
  for (const auto& member : members) {
    // Create a router interface.
    auto router_interface =
        gutil::ParseProtoOrDie<sai::TableEntry>(kRouterInterfaceTemplate);
    const auto& port = member.port;
    router_interface.mutable_router_interface_table_entry()
        ->mutable_match()
        ->set_router_interface_id(absl::StrCat("rif-", port));
    router_interface.mutable_router_interface_table_entry()
        ->mutable_action()
        ->mutable_set_port_and_src_mac()
        ->set_port(absl::StrCat(port));
    router_interface.mutable_router_interface_table_entry()
        ->mutable_action()
        ->mutable_set_port_and_src_mac()
        ->set_src_mac("00:02:03:04:05:06");
    ASSIGN_OR_RETURN(*result.add_updates(),
                     PdTableEntryToPiUpdate(router_interface));

    // Create neighbor entry.
    auto neighbor_entry =
        gutil::ParseProtoOrDie<sai::TableEntry>(kNeighborEntryTemplate);
    neighbor_entry.mutable_neighbor_table_entry()
        ->mutable_match()
        ->set_router_interface_id(absl::StrCat("rif-", port));
    std::string neighbor_id = absl::StrCat("10.0.0.", index++);
    neighbor_entry.mutable_neighbor_table_entry()
        ->mutable_match()
        ->set_neighbor_id(neighbor_id);
    ASSIGN_OR_RETURN(*result.add_updates(),
                     PdTableEntryToPiUpdate(neighbor_entry));

    // Create nexthop table entry.
    auto nexthop_entry =
        gutil::ParseProtoOrDie<sai::TableEntry>(kNextHopTemplate);
    nexthop_entry.mutable_nexthop_table_entry()
        ->mutable_match()
        ->set_nexthop_id(absl::StrCat("nexthop-", port));
    nexthop_entry.mutable_nexthop_table_entry()
        ->mutable_action()
        ->mutable_set_nexthop()
        ->set_router_interface_id(absl::StrCat("rif-", port));
    nexthop_entry.mutable_nexthop_table_entry()
        ->mutable_action()
        ->mutable_set_nexthop()
        ->set_neighbor_id(neighbor_id);
    ASSIGN_OR_RETURN(*result.add_updates(),
                     PdTableEntryToPiUpdate(nexthop_entry));
  }

  // Build group and add members.
  auto group_update =
      gutil::ParseProtoOrDie<sai::TableEntry>(kGroupEntryTemplate);
  for (const auto& member : members) {
    auto port = absl::StrCat(member.port);
    auto member_update =
        gutil::ParseProtoOrDie<sai::WcmpGroupTableEntry::WcmpAction>(
            kMemberEntryTemplate);
    member_update.mutable_action()->mutable_set_nexthop_id()->set_nexthop_id(
        absl::StrCat("nexthop-", port));
    member_update.set_weight(member.weight);
    *group_update.mutable_wcmp_group_table_entry()->add_wcmp_actions() =
        member_update;
  }
  ASSIGN_OR_RETURN(*result.add_updates(), PdTableEntryToPiUpdate(group_update));

  // Add flows to allow destination mac variations.
  auto l3_dst_mac_classifier =
      gutil::ParseProtoOrDie<sai::TableEntry>(kDstMacClassifier);
  for (int i = 0; i < GetNumPackets(/*should_be_hashed=*/false); i++) {
    netaddr::MacAddress netaddr_mac(std::bitset<48>(GetIthDstMac(i)));

    l3_dst_mac_classifier.mutable_l3_admit_table_entry()
        ->mutable_match()
        ->mutable_dst_mac()
        ->set_value(netaddr_mac.ToString());
    l3_dst_mac_classifier.mutable_l3_admit_table_entry()
        ->mutable_match()
        ->mutable_dst_mac()
        ->set_mask("ff:ff:ff:ff:ff:ff");
    ASSIGN_OR_RETURN(*result.add_updates(),
                     PdTableEntryToPiUpdate(l3_dst_mac_classifier));
  }

  // Add minimal set of flows to allow forwarding.
  auto ipv4_fallback =
      gutil::ParseProtoOrDie<sai::TableEntry>(kIpv4DefaultRouteEntry);
  ASSIGN_OR_RETURN(*result.add_updates(),
                   PdTableEntryToPiUpdate(ipv4_fallback));

  auto ipv6_fallback =
      gutil::ParseProtoOrDie<sai::TableEntry>(kIpv6DefaultRouteEntry);
  ASSIGN_OR_RETURN(*result.add_updates(),
                   PdTableEntryToPiUpdate(ipv6_fallback));
  return result;
}

// Returns a human-readable description of a test config.
std::string DescribeTestConfig(const TestConfiguration& config) {
  return absl::StrCat("field=", PacketFieldToString(config.field),
                      " ipv4=", config.ipv4, " encapped=", config.encapped,
                      " inner_ipv4=", config.inner_ipv4,
                      " decap=", config.decap);
}

std::string TestConfigurationToPayload(const TestConfiguration& config) {
  std::string desc = DescribeTestConfig(config);
  if (desc.size() >= 64) return desc;
  // Pad to 64 bytes
  return absl::StrCat(desc, std::string(64 - desc.size(), '.'));
}

// Returns a human-readable description of the observed vs expected
// distribution.
std::string DescribeDistribution(
    absl::Span<const Member> members,
    const absl::flat_hash_map<uint32_t, int>& ports, bool expect_one_port) {
  double total_weight = 0;
  for (const auto& member : members) {
    total_weight += member.weight;
  }
  std::string explanation = "";
  for (const auto& member : members) {
    double actual_count =
        ports.contains(member.port) ? ports.at(member.port) : 0;
    if (expect_one_port) {
      explanation += absl::StrCat("\nport ", member.port,
                                  ": actual_count = ", actual_count);
    } else {
      double expected_count =
          GetNumPackets(true) * member.weight / total_weight;
      explanation +=
          absl::StrCat("\nport ", member.port, " with weight ", member.weight,
                       ": expected_count = ", expected_count,
                       ", actual_count = ", actual_count);
    }
  }
  return explanation;
}

// Returns the i-th packet for the given test configuration.  The packets all
// follow the requirements of the config (e.g., is this a IPv4 or IPv6 packet),
// and vary in exactly one field (the one specified in the config).
absl::StatusOr<PacketlibPacket> GeneratePacket(const TestConfiguration& config,
                                               int index) {
  PacketlibPacket packet;
  const auto& field = config.field;

  EthernetHeader* eth = packet.add_headers()->mutable_ethernet_header();

  uint64_t default_src_mac = 123;
  eth->set_ethernet_source(
      netaddr::MacAddress(std::bitset<48>(default_src_mac)).ToString());
  if (field == ETH_SRC) {
    eth->set_ethernet_source(
        netaddr::MacAddress(
            std::bitset<48>(default_src_mac + index % kMaxMacAddr))
            .ToString());
  }
  eth->set_ethernet_destination(
      netaddr::MacAddress(std::bitset<48>(kBaseDstMac)).ToString());
  if (field == ETH_DST) {
    eth->set_ethernet_destination(
        netaddr::MacAddress(std::bitset<48>(GetIthDstMac(index))).ToString());
  }
  eth->set_ethertype(config.ipv4 ? packetlib::EtherType(ETHERTYPE_IP)
                                 : packetlib::EtherType(ETHERTYPE_IPV6));
  {
    uint8_t hop_limit = 32;
    // Avoid hop_limit of 0,1,2 to avoid drops.
    if (field == HOP_LIMIT) hop_limit = 3 + (index % (256 - 3));

    uint8_t dscp = index % static_cast<uint8_t>(1 << 6);

    int next_protocol = IPPROTO_UDP;
    if (config.encapped)
      next_protocol = config.inner_ipv4 ? IPPROTO_IPIP : IPPROTO_IPV6;

    if (config.ipv4) {
      Ipv4Header* ip = packet.add_headers()->mutable_ipv4_header();
      ip->set_ihl(IpIhl(5));
      uint32_t default_src = config.decap ? kBaseDecapIpV4Src : kBaseIpV4Src;
      if (field == IP_SRC) {
        ip->set_ipv4_source(
            netaddr::Ipv4Address(std::bitset<32>(default_src + index))
                .ToString());
      } else {
        ip->set_ipv4_source(
            netaddr::Ipv4Address(std::bitset<32>(default_src)).ToString());
      }
      auto default_dst = config.decap ? kBaseDecapIpV4Dst : kBaseIpV4Dst;
      if (field == IP_DST) {
        ip->set_ipv4_destination(
            netaddr::Ipv4Address(std::bitset<32>(default_dst + index))
                .ToString());
      } else {
        ip->set_ipv4_destination(
            netaddr::Ipv4Address(std::bitset<32>(default_dst)).ToString());
      }
      ip->set_ttl(IpTtl(hop_limit));
      ip->set_dscp(IpDscp(dscp));
      ip->set_protocol(IpProtocol(next_protocol));

      // Fill other default (required) fields.
      ip->set_ecn(IpEcn(0));
      ip->set_identification(IpIdentification(0));
      ip->set_flags(IpFlags(0));
      ip->set_fragment_offset(IpFragmentOffset(0));
    } else {
      Ipv6Header* ip = packet.add_headers()->mutable_ipv6_header();
      auto default_src = absl::MakeUint128(0x0001000200030004, 0);
      if (field == IP_SRC) {
        ip->set_ipv6_source(
            netaddr::Ipv6Address(default_src + index).ToString());
      } else {
        ip->set_ipv6_source(netaddr::Ipv6Address(default_src).ToString());
      }
      auto default_dst = absl::MakeUint128(0x0002000300040005, 0);
      if (field == IP_DST) {
        ip->set_ipv6_destination(
            netaddr::Ipv6Address(default_dst + index).ToString());
      } else {
        ip->set_ipv6_destination(netaddr::Ipv6Address(default_dst).ToString());
      }
      ip->set_hop_limit(IpHopLimit(hop_limit));
      ip->set_dscp(IpDscp(dscp));
      uint32_t flow_label = 0;
      if (field == FLOW_LABEL_LOWER_16) {
        flow_label = index % (1 << 16);
      }
      if (field == FLOW_LABEL_UPPER_4) {
        flow_label = (index % (1 << 4)) << 16;
      }
      ip->set_flow_label(IpFlowLabel(flow_label));
      ip->set_next_header(IpNextHeader(next_protocol));
      ip->set_ecn(IpEcn(0));
    }
  }

  // Add inner header
  if (config.encapped) {
    uint8_t inner_hop_limit = 33;
    // Avoid hop_limit of 0,1,2 to avoid drops.
    if (field == INNER_HOP_LIMIT) inner_hop_limit = 3 + (index % (256 - 3));

    uint8_t inner_dscp = index % static_cast<uint8_t>(1 << 6);

    if (config.inner_ipv4) {
      Ipv4Header* ip = packet.add_headers()->mutable_ipv4_header();
      ip->set_ihl(IpIhl(5));
      uint32_t default_inner_src = 0x03040506;
      if (field == INNER_IP_SRC) {
        ip->set_ipv4_source(
            netaddr::Ipv4Address(std::bitset<32>(default_inner_src + index))
                .ToString());
      } else {
        ip->set_ipv4_source(
            netaddr::Ipv4Address(std::bitset<32>(default_inner_src))
                .ToString());
      }
      uint32_t default_inner_dst = 0x04050607;
      if (field == INNER_IP_DST) {
        ip->set_ipv4_destination(
            netaddr::Ipv4Address(std::bitset<32>(default_inner_dst + index))
                .ToString());
      } else {
        ip->set_ipv4_destination(
            netaddr::Ipv4Address(std::bitset<32>(default_inner_dst))
                .ToString());
      }
      ip->set_ttl(IpTtl(inner_hop_limit));
      ip->set_dscp(IpDscp(inner_dscp));
      ip->set_protocol(IpProtocol(IPPROTO_UDP));
      // Fill other default (required) fields.
      ip->set_ecn(IpEcn(0));
      ip->set_identification(IpIdentification(0));
      ip->set_flags(IpFlags(0));
      ip->set_fragment_offset(IpFragmentOffset(0));
    } else {
      Ipv6Header* ip = packet.add_headers()->mutable_ipv6_header();
      auto default_inner_src = absl::MakeUint128(0x00030000400050006, 0);
      if (field == INNER_IP_SRC) {
        ip->set_ipv6_source(
            netaddr::Ipv6Address(default_inner_src + index).ToString());
      } else {
        ip->set_ipv6_source(netaddr::Ipv6Address(default_inner_src).ToString());
      }
      auto default_inner_dst = absl::MakeUint128(0x0004000500060007, 0);
      if (field == INNER_IP_DST) {
        ip->set_ipv6_destination(
            netaddr::Ipv6Address(default_inner_dst + index).ToString());
      } else {
        ip->set_ipv6_destination(
            netaddr::Ipv6Address(default_inner_dst).ToString());
      }
      ip->set_hop_limit(IpTtl(inner_hop_limit));
      ip->set_dscp(IpDscp(inner_dscp));
      uint32_t inner_flow_label = 0;
      if (field == INNER_FLOW_LABEL_LOWER_16) {
        inner_flow_label = index % (1 << 16);
      }
      if (field == INNER_FLOW_LABEL_UPPER_4) {
        inner_flow_label = (index % (1 << 4)) << 16;
      }
      ip->set_flow_label(IpFlowLabel(inner_flow_label));
      ip->set_next_header(IpProtocol(IPPROTO_UDP));
      ip->set_ecn(IpEcn(0));
    }
  }

  UdpHeader* udp = packet.add_headers()->mutable_udp_header();
  uint32_t default_src_port = 2345;
  uint32_t default_dst_port = 4567;
  udp->set_source_port(UdpPort(default_src_port));
  if (field == L4_SRC_PORT) {
    udp->set_source_port(UdpPort(GetIthL4Port(index, default_src_port)));
  }
  udp->set_destination_port(UdpPort(default_dst_port));
  if (field == L4_DST_PORT) {
    udp->set_destination_port(UdpPort(GetIthL4Port(index, default_dst_port)));
  }

  packet.set_payload(TestConfigurationToPayload(config));

  return packet;
}

// Generate N random weights that add up to max_weight, with at least 1 in each
// bucket.
absl::StatusOr<std::vector<int>> GenerateNRandomWeights(const int n,
                                                        const int max_weight,
                                                        absl::BitGen& gen) {
  if (n > max_weight || n <= 0) {
    return absl::InvalidArgumentError("Invalid input argument");
  }

  std::vector<int> weights(n, 1);
  for (int i = 0; i < (max_weight - n); i++) {
    int x = absl::uniform_int_distribution<int>(0, n - 1)(gen);
    weights.at(x)++;
  }
  return weights;
}

// TODO: Temporary fix to rescale TH3 weights.
// To be removed when 256 member support is available.
int Th3RescaleWeights(const int weight) {
  if (weight <= 1) {
    return weight;
  }
  if (weight == 2) {
    return 1;
  }
  return (weight - 1) / 2;
}

// Generate all possible test configurations, send packets for every config, and
// check that the observed distribution is correct.
TEST_P(HashingTestFixture, SendPacketsToWcmpGroupsAndCheckDistribution) {
  LOG(INFO) << "Starting actual test";

  thinkit::MirrorTestbed& testbed =
      GetParam().mirror_testbed->GetMirrorTestbed();

  const std::string& gnmi_config = GetParam().gnmi_config;
  ASSERT_OK(
      testbed.Environment().StoreTestArtifact("gnmi_config.txt", gnmi_config));

  ASSERT_TRUE(GetParam().port_ids.has_value())
      << "Controller port ids (required) not provided.";
  std::vector<int> orion_port_ids = GetParam().port_ids.value();
  ASSERT_GE(orion_port_ids.size(), kNumWcmpMembersForTest);

  // The port on which we input all dataplane test packets.
  const int ingress_port = orion_port_ids[0];
  // Obtain P4Info for SAI P4 program.
  const p4::config::v1::P4Info p4info =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);

  ASSERT_OK(SaveProto(testbed, p4info, "p4info.pb.txt"));
  ASSERT_OK_AND_ASSIGN(const pdpi::IrP4Info ir_p4info,
                       pdpi::CreateIrP4Info(p4info));

  // Setup SUT & control switch.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> sut_p4_session,
                       pdpi::P4RuntimeSession::Create(testbed.Sut()));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> control_p4_session,
      pdpi::P4RuntimeSession::Create(testbed.ControlSwitch()));

  ASSERT_OK(pins_test::PushGnmiConfig(testbed.Sut(), gnmi_config));
  ASSERT_OK(pins_test::PushGnmiConfig(testbed.ControlSwitch(), gnmi_config));
  ASSERT_OK(SetUpSut(sut_p4_session.get()));
  ASSERT_OK(SetUpControlSwitch(control_p4_session.get()));

  // Listen for packets from the SUT on the ControlSwitch.
  TestData test_data;
  auto ReceivePacketFiber = [&]() {
    p4::v1::StreamMessageResponse pi_response;
    // The only way to break out of this loop is for the stream channel to
    // be closed. gRPC does not support selecting on both stream Read and
    // fiber Cancel.
    while (control_p4_session->StreamChannelRead(pi_response)) {
      absl::MutexLock lock(&test_data.mutex);
      sai::StreamMessageResponse pd_response;
      ASSERT_OK(pdpi::PiStreamMessageResponseToPd(ir_p4info, pi_response,
                                                  &pd_response))
          << " PacketIn PI to PD failed: ";
      ASSERT_TRUE(pd_response.has_packet())
          << " Received unexpected stream message for packet in: "
          << pd_response.DebugString();
      absl::string_view raw_packet = pd_response.packet().payload();
      Packet packet;
      packet.set_port(pd_response.packet().metadata().ingress_port());
      packet.set_hex(absl::BytesToHexString(raw_packet));
      *packet.mutable_parsed() = packetlib::ParsePacket(raw_packet);
      std::string key = packet.parsed().payload();
      if (test_data.data.contains(key)) {
        test_data.data[key].output.push_back(packet);
        test_data.total_packets_received += 1;
      } else {
        if ((testbed.Environment().MaskKnownFailures() ||
             absl::GetFlag(FLAGS_ignore_router_solicitation_packets)) &&
            packet.parsed().headers().size() == 3 &&
            packet.parsed().headers(2).icmp_header().type() == "0x85") {
          ASSERT_OK(AppendProto(testbed, packet,
                                "control_unexpected_packet_ins.pb.txt"));
        } else {
          test_data.total_invalid_packets_received += 1;
        }
      }
    }
  };
  std::thread receive_packet_fiber(ReceivePacketFiber);
  absl::BitGen gen;
  // Iterate over 3 sets of random weights for 3 ports.
  for (int iter = 0; iter < 3; iter++) {
    std::vector<Member> members(kNumWcmpMembersForTest);
    for (int i = 0; i < kNumWcmpMembersForTest; i++) {
      members[i] = Member{.weight = 0, .port = orion_port_ids[i]};
    }

    std::vector<int> weights(kNumWcmpMembersForTest);
    if (iter == 0) {
      // Run for ECMP (all weights=1) for first iteration.
      for (int i = 0; i < kNumWcmpMembersForTest; i++) {
        weights[i] = 1;
      }
    } else {
      // Max weights is set to 30 (15 after TH3 re-scaling) to limit
      // the number of packets required for this testing to be < 10k.
      // See go/p4-hashing (section How many packets do we need?).
      ASSERT_OK_AND_ASSIGN(weights,
                           GenerateNRandomWeights(kNumWcmpMembersForTest,
                                                  /*max_weight=*/30, gen));
    }
    for (int i = 0; i < members.size(); i++) {
      members.at(i).weight = weights.at(i);
    }

    ASSERT_OK_AND_ASSIGN(auto write_request, GetHashingEntities(members));
    // Save entities.
    EXPECT_OK(SaveProto(testbed, write_request,
                        absl::StrCat("flows.pi.", iter, ".txt")));
    ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(sut_p4_session.get(),
                                                     write_request));

    // TODO: Rescale the member weights to <=128 for now to match
    // Hardware behaviour, remove when hardware supports > 128 weights.
    for (int i = 0; i < members.size(); i++) {
      members.at(i).weight = Th3RescaleWeights(members.at(i).weight);
      LOG(INFO) << "Rescaling member id: " << members.at(i).port
                << " from weight: " << weights.at(i)
                << " to new weight: " << members.at(i).weight;
    }

    // Generate test configuration and send packets.
    std::vector<TestConfiguration> configs;
    absl::Time start = absl::Now();
    int total_packets = 0;
    for (bool ipv4 : {true, false}) {
      for (bool encapped : {false}) {
        for (bool inner_ipv4 : {false}) {
          for (bool decap : {false}) {
            for (PacketField field : AllFields()) {
              // TODO: The switch currently hashes the upper bits
              // of flow label, so we just skip them here.
              if (field == FLOW_LABEL_UPPER_4 ||
                  field == INNER_FLOW_LABEL_UPPER_4)
                continue;
              TestConfiguration config = {field, ipv4, encapped, inner_ipv4,
                                          decap};
              if (!IsValidTestConfiguration(config)) continue;
              configs.push_back(config);

              // Create test data entry.
              std::string key = TestConfigurationToPayload(config);
              {
                absl::MutexLock lock(&test_data.mutex);
                TestInputOutput inout;
                inout.config = config;
                test_data.data.insert({key, inout});
              }

              // Send packets to the switch.
              std::string packet_log = "";

              for (int idx = 0; idx < GetNumPackets(config) + kNumExtraPackets;
                   idx++) {
                // Rate limit to 500 packets per second.
                auto now = absl::Now();
                auto earliest_send_time =
                    start + (total_packets * absl::Seconds(1) / 500.0);
                if (earliest_send_time > now) {
                  absl::SleepFor(earliest_send_time - now);
                }

                int port = ingress_port;
                if (field == INPUT_PORT) {
                  port = orion_port_ids[idx % members.size()];
                }

                ASSERT_OK_AND_ASSIGN(auto packet, GeneratePacket(config, idx));
                ASSERT_OK_AND_ASSIGN(auto raw_packet, SerializePacket(packet));
                ASSERT_OK_AND_ASSIGN(std::string port_string,
                                     pdpi::IntToDecimalString(port));
                ASSERT_OK(InjectEgressPacket(port_string, raw_packet, ir_p4info,
                                             control_p4_session.get()));

                total_packets++;

                Packet p;
                p.set_port(absl::StrCat(port));
                *p.mutable_parsed() = packet;
                p.set_hex(absl::BytesToHexString(raw_packet));
                packet_log += absl::StrCat(p.DebugString(), "\n\n");
              }

              // Save log of packets.
              EXPECT_OK(testbed.Environment().StoreTestArtifact(
                  absl::StrCat(
                      "packets-for-config-", iter, "-",
                      absl::StrJoin(
                          absl::StrSplit(DescribeTestConfig(config), " "), "-"),
                      ".txt"),
                  packet_log));
            }
          }
        }
      }
    }

    LOG(INFO) << "Sent " << total_packets << " packets in "
              << (absl::Now() - start) << ".";

    // Wait for packets from the SUT to arrive.
    absl::SleepFor(kDurationToWaitForPacketsFromSut);

    // Clear table entries.
    {
      auto start = absl::Now();
      EXPECT_OK(pdpi::ClearTableEntries(sut_p4_session.get(), ir_p4info));
      LOG(INFO) << "Cleared table entries on SUT in " << (absl::Now() - start);
    }

    // For each test configuration, check the output distribution.
    {
      absl::MutexLock lock(&test_data.mutex);
      for (const auto& config : configs) {
        const auto& key = TestConfigurationToPayload(config);
        const TestInputOutput& test = test_data.data[key];
        auto n_packets = GetNumPackets(config);
        EXPECT_GE(test.output.size(), n_packets)
            << "Not enough packets received for " << DescribeTestConfig(config);

        // Proceed with the actual number of packets received
        n_packets = test.output.size();
        if (n_packets == 0) continue;

        // Count packets per port
        absl::flat_hash_map<uint32_t, int> ports;
        for (const auto& output : test.output) {
          ASSERT_OK_AND_ASSIGN(uint32_t out_port,
                               pdpi::DecimalStringToUint32(output.port()));
          ports[out_port] += 1;
        }

        // Check we only saw expected ports.
        for (const auto& port : ports) {
          bool port_is_memberport = false;
          for (const auto& member : members) {
            port_is_memberport |= port.first == member.port;
          }
          EXPECT_TRUE(port_is_memberport) << "Unexpected port: " << port.first;
        }

        LOG(INFO) << "Results for " << DescribeTestConfig(config) << ":";
        LOG(INFO) << "- received " << test.output.size() << " packets";
        LOG(INFO) << "- observed distribution was:"
                  << DescribeDistribution(members, ports,
                                          !PacketsShouldBeHashed(config));

        if (PacketsShouldBeHashed(config)) {
          double total_weight = 0;
          for (const auto& member : members) {
            total_weight += member.weight;
          }

          // Compute chi squared value (see go/p4-hashing for the formula).
          double chi_square = 0;
          for (const auto& member : members) {
            double expected_count = n_packets * member.weight / total_weight;
            double actual_count = ports[member.port];
            double diff = actual_count - expected_count;
            chi_square += diff * diff / expected_count;
          }

          // DOF = total weight - 1
          const int degrees_of_freedom = total_weight - 1;
          // Check p-value against threshold.
          double pvalue =
              1.0 -
              (boost::math::cdf(boost::math::chi_squared(degrees_of_freedom),
                                chi_square));
          LOG(INFO) << "- chi square is " << chi_square;
          LOG(INFO) << "- p-value is " << pvalue;
          if ((config.field != L4_SRC_PORT && config.field != L4_DST_PORT) ||
              !absl::GetFlag(FLAGS_ignore_l4_port_hashing)) {
            EXPECT_GT(pvalue, 0.001)
                << "For config " << DescribeTestConfig(config) << ": "
                << "The p-value is small enough that we reject the "
                   "null-hypothesis "
                   "(H_0 = 'The switch distribution is correct'), and instead "
                   "have strong evidence that switch produces the wrong "
                   "distribution:"
                << DescribeDistribution(members, ports,
                                        /*expect_one_port=*/false);
          }
        } else {
          LOG(INFO) << "- packets were forwarded to " << ports.size()
                    << " ports";
          // Expect all packets to be forwarded to the same port.
          EXPECT_EQ(ports.size(), 1)
              << "Expected the test configuration " << std::endl
              << DescribeTestConfig(config) << std::endl
              << "to not influence the hash, and thus all packets should be "
                 "forwarded on a single port.  Instead, the following was "
                 "observed: "
              << DescribeDistribution(members, ports, /*expect_one_port=*/true);
        }
      }

      LOG(INFO) << "Number of sent packets:               " << total_packets;
      LOG(INFO) << "Number of received packets (valid):   "
                << test_data.total_packets_received;
      LOG(INFO) << "Number of received packets (invalid): "
                << test_data.total_invalid_packets_received;

      // Clear TestData so that it can used by the next set of members.
      test_data.data.clear();
      test_data.total_packets_received = 0;
      test_data.total_invalid_packets_received = 0;
    }
  }

  // Stop RPC sessions.
  control_p4_session->TryCancel();
  receive_packet_fiber.join();
  sut_p4_session->TryCancel();
}

}  // namespace
}  // namespace gpins

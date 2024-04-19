// Copyright (c) 2021, Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tests/packet_forwarding_tests.h"

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/collections.h"
#include "gutil/proto.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/basic_traffic/basic_p4rt_util.h"
#include "lib/basic_traffic/basic_traffic.h"
#include "lib/gnmi/gnmi_helper.h"
#include "lib/utils/generic_testbed_utils.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "p4_pdpi/netaddr/ipv6_address.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/pd.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "thinkit/control_device.h"
#include "thinkit/generic_testbed.h"
#include "thinkit/proto/generic_testbed.pb.h"
#include "thinkit/switch.h"

namespace pins_test {
namespace {

// Test packet proto message sent from control switch to sut.
constexpr absl::string_view kTestPacket = R"pb(
  headers {
    ethernet_header {
      ethernet_destination: "02:03:04:05:06:07"
      ethernet_source: "00:01:02:03:04:05"
      ethertype: "0x0800"
    }
  }
  headers {
    ipv4_header {
      version: "0x4"
      ihl: "0x5"
      dscp: "0x03"
      ecn: "0x0"
      identification: "0x0000"
      flags: "0x0"
      fragment_offset: "0x0000"
      ttl: "0x20"
      protocol: "0x11"
      ipv4_source: "1.2.3.4"
      ipv4_destination: "$0"
    }
  }
  headers { udp_header { source_port: "0x0000" destination_port: "0x0000" } }
  payload: "Basic L3 test packet")pb";

// Sets up route from source port to destination port on sut.
absl::Status SetupRoute(pdpi::P4RuntimeSession& p4_session, int src_port_id,
                        int dst_port_id) {
  RETURN_IF_ERROR(basic_traffic::ProgramTrafficVrf(&p4_session));
  RETURN_IF_ERROR(
      basic_traffic::ProgramRouterInterface(&p4_session, src_port_id));
  RETURN_IF_ERROR(
      basic_traffic::ProgramRouterInterface(&p4_session, dst_port_id));
  return basic_traffic::ProgramIPv4Route(&p4_session, dst_port_id);
}

TEST_P(PacketForwardingTestFixture, PacketForwardingTest) {
  thinkit::TestRequirements requirements =
      gutil::ParseProtoOrDie<thinkit::TestRequirements>(
          R"pb(interface_requirements {
                 count: 2
                 interface_mode: CONTROL_INTERFACE
               })pb");

  ASSERT_OK_AND_ASSIGN(auto testbed, GetTestbedWithRequirements(requirements));
  std::vector<InterfaceLink> control_links =
      FromTestbed(GetAllControlLinks, *testbed);

  ASSERT_OK_AND_ASSIGN(auto stub, testbed->Sut().CreateGnmiStub());
  ASSERT_OK_AND_ASSIGN(auto port_id_by_interface,
                       GetAllInterfaceNameToPortId(*stub));

  // Set the `source_link` to the first SUT control link.
  const InterfaceLink& source_link = control_links[0];
  ASSERT_OK_AND_ASSIGN(
      std::string source_port_id_value,
      gutil::FindOrStatus(port_id_by_interface, source_link.sut_interface));
  int source_port_id;
  ASSERT_TRUE(absl::SimpleAtoi(source_port_id_value, &source_port_id));

  // Set the `destination_link` to the second SUT control link.
  const InterfaceLink& destination_link = control_links[1];
  ASSERT_OK_AND_ASSIGN(std::string destination_port_id_value,
                       gutil::FindOrStatus(port_id_by_interface,
                                           destination_link.sut_interface));
  int destination_port_id;
  ASSERT_TRUE(
      absl::SimpleAtoi(destination_port_id_value, &destination_port_id));

  LOG(INFO) << "Source port: " << source_link.sut_interface
            << " port id: " << source_port_id;
  LOG(INFO) << "Destination port: " << destination_link.sut_interface
            << " port id: " << destination_port_id;

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> p4_session,
      pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
          testbed->Sut(), sai::GetP4Info(sai::Instantiation::kMiddleblock)));

  // Set up a route between the source and destination interfaces.
  ASSERT_OK(SetupRoute(*p4_session, source_port_id, destination_port_id));

  // Make test packet based on destination port ID.
  const auto test_packet =
      gutil::ParseProtoOrDie<packetlib::Packet>(absl::Substitute(
          kTestPacket, basic_traffic::PortIdToIP(destination_port_id)));
  ASSERT_OK_AND_ASSIGN(std::string test_packet_data,
                       packetlib::SerializePacket(test_packet));

  absl::Mutex mutex;
  std::vector<std::string> received_packets;
  static constexpr int kPacketsToSend = 10;
  {
    ASSERT_OK_AND_ASSIGN(
        auto finalizer,
        testbed.get()->ControlDevice().CollectPackets(
            [&](absl::string_view interface, absl::string_view packet) {
              if (interface != destination_link.peer_interface) {
                return;
              }
              packetlib::Packet parsed_packet = packetlib::ParsePacket(packet);
              if (!absl::StrContains(parsed_packet.payload(),
                                     "Basic L3 test packet")) {
                return;
              }
              absl::MutexLock lock(&mutex);
              received_packets.push_back(std::string(packet));
            }));

    LOG(INFO) << "Sending Packet to " << source_link.peer_interface;
    LOG(INFO) << "Test packet data: " << test_packet.DebugString();

    for (int i = 0; i < kPacketsToSend; i++) {
      // Send packet to SUT.
      ASSERT_OK(testbed->ControlDevice().SendPacket(source_link.peer_interface,
                                                    test_packet_data))
          << "failed to inject the packet.";
      LOG(INFO) << "SendPacket completed";
    }
    absl::SleepFor(absl::Seconds(30));
  }
  EXPECT_EQ(received_packets.size(), kPacketsToSend);
}

TEST_P(PacketForwardingTestFixture, AllPortsPacketForwardingTest) {
  thinkit::TestRequirements requirements =
      gutil::ParseProtoOrDie<thinkit::TestRequirements>(
          R"pb(interface_requirements {
                 count: 2
                 interface_mode: CONTROL_INTERFACE
               })pb");

  ASSERT_OK_AND_ASSIGN(auto testbed, GetTestbedWithRequirements(requirements));
  std::vector<std::string> sut_interfaces =
      GetSutInterfaces(FromTestbed(GetAllControlLinks, *testbed));

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> p4_session,
      pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
          testbed->Sut(), sai::GetP4Info(sai::Instantiation::kMiddleblock)));

  const auto test_packet =
      gutil::ParseProtoOrDie<packetlib::Packet>(kTestPacket);
  ASSERT_OK_AND_ASSIGN(
      auto statistics,
      basic_traffic::SendTraffic(*testbed, p4_session.get(),
                                 basic_traffic::AllToAll(sut_interfaces),
                                 {test_packet}, absl::Minutes(5)));
  for (const basic_traffic::TrafficStatistic& statistic : statistics) {
    LOG(INFO) << statistic.interfaces.ingress_interface << " -> "
              << statistic.interfaces.egress_interface << "\n"
              << statistic.packet.DebugString() << "\n"
              << ": Packets sent: " << statistic.packets_sent
              << ", Packets received: " << statistic.packets_received;
    EXPECT_EQ(statistic.packets_sent, statistic.packets_received);
    EXPECT_EQ(statistic.packets_routed_incorrectly, 0);
  }
}

}  // namespace
}  // namespace pins_test

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
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/gnmi/gnmi_helper.h"
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

constexpr absl::string_view kDefaultVrf = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554689
        action {
          action {
            action_id: 16777472
            params { param_id: 1 value: "default-vrf" }
          }
        }
        priority: 1132
      }
    }
  }
)pb";

constexpr absl::string_view kIngressRouterInterface = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554497
        match {
          field_id: 1
          exact { value: "ingress-router-interface" }
        }
        action {
          action {
            action_id: 16777218
            params { param_id: 1 value: "$0" }
            params { param_id: 2 value: "\000\000\000\000\000\$1" }
          }
        }
      }
    }
  }
)pb";

constexpr absl::string_view kEgressRouterInterface = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554497
        match {
          field_id: 1
          exact { value: "egress-router-interface" }
        }
        action {
          action {
            action_id: 16777218
            params { param_id: 1 value: "$0" }
            params { param_id: 2 value: "\000\000\000\000\000\$1" }
          }
        }
      }
    }
  }
)pb";

constexpr absl::string_view kNeighborEntry = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554496
        match {
          field_id: 1
          exact { value: "egress-router-interface" }
        }
        match {
          field_id: 2
          exact { value: "10.0.0.1" }
        }
        action {
          action {
            action_id: 16777217
            params { param_id: 1 value: "\000\032\021\027_\200" }
          }
        }
      }
    }
  }
)pb";

constexpr absl::string_view kNexthopEntry = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554498
        match {
          field_id: 1
          exact { value: "nexthop-1" }
        }
        action {
          action {
            action_id: 16777219
            params { param_id: 1 value: "egress-router-interface" }
            params { param_id: 2 value: "10.0.0.1" }
          }
        }
      }
    }
  }
)pb";

constexpr absl::string_view kIp4tableEntry = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554500
        match {
          field_id: 1
          exact { value: "default-vrf" }
        }
        match {
          field_id: 2
          lpm { value: "\x0a\x00\x00\x01" prefix_len: 32 }
        }
        action {
          action {
            action_id: 16777221
            params { param_id: 1 value: "nexthop-1" }
          }
        }
      }
    }
  }
)pb";

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
      total_length: "0x00d4"
      identification: "0x0000"
      flags: "0x0"
      fragment_offset: "0x0000"
      ttl: "0x20"
      protocol: "0x11"
      checksum: "0x8c07"
      ipv4_source: "1.2.3.4"
      ipv4_destination: "10.0.0.1"
    }
  }
  headers {
    udp_header {
      source_port: "0x0000"
      destination_port: "0x0000"
      length: "0x00c0"
      checksum: "0xd557"
    }
  }
  payload: "test packet #1: p4-symbolic: ipv4_table_entry { match { vrf_id: \"default-vrf\" ipv4_dst { value: \"10.0.0.1\" prefix_length: 32 } } action { set_nexthop_id { nexthop_id: \"nexthop-1\" } } }")pb";

// Creates mac address for the interface based on it's port id.
std::string CreateMacAddress(int port_id) {
  if (port_id < 10) {
    return absl::StrCat("00", port_id);
  } else if (port_id < 100) {
    return absl::StrCat("0", port_id);
  } else {
    return absl::StrCat(port_id % 1000);
  }
}

// Sets the request type and sends a write request.
absl::Status ProgramRequest(pdpi::P4RuntimeSession& p4_session,
                            absl::string_view request_str) {
  p4::v1::WriteRequest request;
  RETURN_IF_ERROR(gutil::ReadProtoFromString(request_str, &request));
  request.mutable_updates(0)->set_type(p4::v1::Update::INSERT);
  RETURN_IF_ERROR(pdpi::SetMetadataAndSendPiWriteRequest(&p4_session, request))
      << "Failed to program the request: " << request.ShortDebugString();
  return absl::OkStatus();
}

// Sets up route from source port to destination port on sut.
absl::Status SetupRoute(pdpi::P4RuntimeSession& p4_session, int src_port_id,
                        int des_port_id) {
  LOG(INFO) << "Setup Route on sut from port: " << src_port_id << " to "
            << des_port_id;
  LOG(INFO) << "Program request default_vrf.";
  RETURN_IF_ERROR(ProgramRequest(p4_session, kDefaultVrf));

  LOG(INFO) << "Program request ingress_router_interface.";
  RETURN_IF_ERROR(ProgramRequest(
      p4_session, absl::Substitute(kIngressRouterInterface, src_port_id,
                                   CreateMacAddress(src_port_id))));
  LOG(INFO) << "Program request egress_router_interface.";
  RETURN_IF_ERROR(ProgramRequest(
      p4_session, absl::Substitute(kEgressRouterInterface, des_port_id,
                                   CreateMacAddress(des_port_id))));

  // Create neighbor object.
  LOG(INFO) << "Program request neightbor_entry.";
  RETURN_IF_ERROR(ProgramRequest(p4_session, kNeighborEntry));

  // Create nexthop table entry.
  LOG(INFO) << "Program request nexthop_entry.";
  RETURN_IF_ERROR(ProgramRequest(p4_session, kNexthopEntry));
  LOG(INFO) << "Program request ip4table_entry.";
  RETURN_IF_ERROR(ProgramRequest(p4_session, kIp4tableEntry));
  LOG(INFO) << "Setup route complete.";
  return absl::OkStatus();
}

TEST_P(PacketForwardingTestFixture, PacketForwardingTest) {
  thinkit::TestRequirements requirements =
      gutil::ParseProtoOrDie<thinkit::TestRequirements>(
          R"pb(interface_requirements {
                 count: 2
                 interface_mode: CONTROL_INTERFACE
               })pb");

  ASSERT_OK_AND_ASSIGN(auto testbed, GetTestbedWithRequirements(requirements));
  std::vector<std::string> sut_interfaces;
  std::vector<std::string> peer_interfaces;
  std::vector<int> sut_interface_ports;

  ASSERT_OK_AND_ASSIGN(auto stub, testbed->Sut().CreateGnmiStub());
  ASSERT_OK_AND_ASSIGN(auto port_id_by_interface,
                       GetAllInterfaceNameToPortId(*stub));
  for (const auto& [interface, info] : testbed->GetSutInterfaceInfo()) {
    if (info.interface_mode == thinkit::CONTROL_INTERFACE) {
      int port_id;
      EXPECT_TRUE(absl::SimpleAtoi(port_id_by_interface[interface], &port_id));
      LOG(INFO) << "@ " << interface << ":" << info.peer_interface_name << ":"
                << port_id;
      sut_interfaces.push_back(interface);
      peer_interfaces.push_back(info.peer_interface_name);
      sut_interface_ports.push_back(port_id);
    }
    if (sut_interfaces.size() == 2) {
      break;
    }
  }

  LOG(INFO) << "Source port: " << sut_interfaces[0]
            << " port id: " << sut_interface_ports[0];
  LOG(INFO) << "Destination port: " << sut_interfaces[1]
            << " port id: " << sut_interface_ports[1];

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> p4_session,
      pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
          testbed->Sut(), sai::GetP4Info(sai::Instantiation::kMiddleblock)));

  // Sets up a route from first to second port.
  ASSERT_OK(
      SetupRoute(*p4_session, sut_interface_ports[0], sut_interface_ports[1]));

  // Makes test packets.
  const auto test_packet =
      gutil::ParseProtoOrDie<packetlib::Packet>(kTestPacket);
  ASSERT_OK_AND_ASSIGN(std::string test_packet_data,
                       packetlib::SerializePacket(test_packet));

  absl::Mutex mutex;
  std::vector<std::string> received_packets;

  {
    ASSERT_OK_AND_ASSIGN(
        auto finalizer,
        testbed.get()->ControlDevice().CollectPackets(
            [&](absl::string_view interface, absl::string_view packet) {
              if (interface == peer_interfaces[1]) {
                absl::MutexLock lock(&mutex);
                received_packets.push_back(std::string(packet));
              }
            }));

    LOG(INFO) << "Sending Packet to " << peer_interfaces[0];
    LOG(INFO) << "Test packet data: " << test_packet.DebugString();

    for (int i = 0; i < 10; i++) {
      // Send packet to SUT.
      ASSERT_OK(testbed->ControlDevice().SendPacket(peer_interfaces[0],
                                                    test_packet_data))
          << "failed to inject the packet.";
      LOG(INFO) << "SendPacket completed";
    }
    absl::SleepFor(absl::Seconds(30));
  }
  EXPECT_EQ(received_packets.size(), 10);
}

}  // namespace
}  // namespace pins_test

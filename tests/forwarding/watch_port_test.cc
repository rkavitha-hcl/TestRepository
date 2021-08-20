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

#include "tests/forwarding/watch_port_test.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
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
#include "p4_pdpi/pd.h"
#include "p4_pdpi/string_encodings/decimal_string.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/packet_test_util.h"
#include "tests/forwarding/util.h"
#include "thinkit/mirror_testbed_fixture.h"
// Tests for the watchport functionality in Action Profile Group operation.

namespace gpins {

namespace {
// Group id used in this test.
constexpr absl::string_view kGroupId = "group-1";

// Vrf used in the test.
constexpr absl::string_view kVrfId = "vrf-1";

// Time to wait after which received packets are processed.
constexpr absl::Duration kDurationToWaitForPackets = absl::Seconds(5);

// Number of members used in the test.
constexpr int kNumWcmpMembersForTest = 3;

// Number of packets used in the test.
constexpr int kNumTestPackets = 5000;

// Helper function to program V4, V6 default route entries.
absl::Status ProgramDefaultRoutes(pdpi::P4RuntimeSession& p4_session,
                                  const pdpi::IrP4Info& ir_p4info,
                                  absl::string_view default_vrf) {
  std::vector<p4::v1::TableEntry> pi_entries;
  // Add minimal set of flows to allow forwarding.
  auto ipv4_fallback = gutil::ParseProtoOrDie<sai::TableEntry>(absl::Substitute(
      R"pb(
        ipv4_table_entry {
          match { vrf_id: "$0" }
          action { set_wcmp_group_id { wcmp_group_id: "$1" } }
        })pb",
      default_vrf, kGroupId));
  auto ipv6_fallback = gutil::ParseProtoOrDie<sai::TableEntry>(
      absl::Substitute(R"pb(
                         ipv6_table_entry {
                           match { vrf_id: "$0" }
                           action { set_wcmp_group_id { wcmp_group_id: "$1" } }
                         })pb",
                       default_vrf, kGroupId));

  for (const auto& pd_entry : {ipv4_fallback, ipv6_fallback}) {
    ASSIGN_OR_RETURN(p4::v1::TableEntry pi_entry,
                     pdpi::PdTableEntryToPi(ir_p4info, pd_entry),
                     _.SetPrepend()
                         << "Failed in PD table conversion to PI, entry: "
                         << pd_entry.DebugString() << " error: ");
    pi_entries.push_back(pi_entry);
  }

  return pdpi::InstallPiTableEntries(&p4_session, ir_p4info, pi_entries);
}

// Push P4Info and install a default vrf for all packets on the SUT.
absl::Status SetUpSut(pdpi::P4RuntimeSession& p4_session,
                      const p4::config::v1::P4Info& p4info,
                      const pdpi::IrP4Info& ir_p4info,
                      absl::string_view default_vrf) {
  RETURN_IF_ERROR(
      pdpi::SetForwardingPipelineConfig(
          &p4_session,
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4info))
          .SetPrepend()
      << "Failed to push P4Info for Sut: ";
  RETURN_IF_ERROR(pdpi::ClearTableEntries(&p4_session, ir_p4info));

  // Set default VRF for all packets.
  ASSIGN_OR_RETURN(
      p4::v1::TableEntry pi_entry,
      pdpi::PdTableEntryToPi(
          ir_p4info, gutil::ParseProtoOrDie<sai::TableEntry>(absl::Substitute(
                         R"pb(
                           acl_pre_ingress_table_entry {
                             match {}  # Wildcard match
                             action { set_vrf { vrf_id: "$0" } }  # Default vrf
                             priority: 1129
                           })pb",
                         default_vrf))));

  return pdpi::InstallPiTableEntry(&p4_session, pi_entry);
}

// Push P4Info and punt all packets on the control switch.
absl::Status SetUpControlSwitch(pdpi::P4RuntimeSession& p4_session,
                                const p4::config::v1::P4Info& p4info,
                                const pdpi::IrP4Info& ir_p4info) {
  RETURN_IF_ERROR(
      pdpi::SetForwardingPipelineConfig(
          &p4_session,
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4info))
          .SetPrepend()
      << "Failed to push P4Info for Control switch: ";
  RETURN_IF_ERROR(pdpi::ClearTableEntries(&p4_session, ir_p4info));
  // Trap all packets on control switch.
  ASSIGN_OR_RETURN(
      p4::v1::TableEntry punt_all_pi_entry,
      pdpi::PdTableEntryToPi(ir_p4info, gutil::ParseProtoOrDie<sai::TableEntry>(
                                            R"pb(
                                              acl_ingress_table_entry {
                                                match {}  # Wildcard match.
                                                action {
                                                  trap { qos_queue: "0x1" }
                                                }            # Action: punt.
                                                priority: 1  # Highest priority.
                                              }
                                            )pb")));
  return pdpi::InstallPiTableEntry(&p4_session, punt_all_pi_entry);
}

// Create members by filling in the controller port ids and random weights for
// each member that add upto 30(random).
absl::StatusOr<std::vector<Member>> CreateMembers(
    absl::Span<const int> controller_port_ids) {
  std::vector<Member> members;
  for (int i = 0; i < kNumWcmpMembersForTest; i++) {
    members.push_back(
        gpins::Member{.weight = 0, .port = controller_port_ids[i]});
  }

  ASSIGN_OR_RETURN(std::vector<int> weights,
                   GenerateNRandomWeights(kNumWcmpMembersForTest,
                                          /*total_weight=*/30));
  for (int i = 0; i < members.size(); i++) {
    members[i].weight = weights[i];
  }
  return members;
}

// Send N packets from the control switch to sut at a rate of 500 packets/sec.
absl::Status SendNPacketsToSut(int num_packets,
                               const TestConfiguration& test_config,
                               absl::Span<const Member> members,
                               absl::Span<const int> port_ids,
                               const pdpi::IrP4Info& ir_p4info,
                               pdpi::P4RuntimeSession& p4_session,
                               thinkit::TestEnvironment& test_environment) {
  const absl::Time start_time = absl::Now();
  for (int i = 0; i < num_packets; i++) {
    // Rate limit to 500 packets per second.
    auto earliest_send_time = start_time + (i * absl::Seconds(1) / 500.0);
    absl::SleepFor(earliest_send_time - absl::Now());

    // Vary the port on which to send the packet if the hash field selected is
    // input port.
    int port = port_ids[0];
    if (test_config.field == PacketField::kInputPort) {
      port = port_ids[i % members.size()];
    }

    ASSIGN_OR_RETURN(packetlib::Packet packet,
                     gpins::GenerateIthPacket(test_config, i));
    ASSIGN_OR_RETURN(std::string raw_packet, SerializePacket(packet));
    ASSIGN_OR_RETURN(std::string port_string, pdpi::IntToDecimalString(port));
    RETURN_IF_ERROR(
        InjectEgressPacket(port_string, raw_packet, ir_p4info, &p4_session));

    gpins::Packet p;
    p.set_port(port_string);
    *p.mutable_parsed() = packet;
    p.set_hex(absl::BytesToHexString(raw_packet));
    // Save log of packets.
    RETURN_IF_ERROR(test_environment.AppendToTestArtifact(
        absl::StrCat(
            "packets-for-config-",
            absl::StrJoin(absl::StrSplit(DescribeTestConfig(test_config), " "),
                          "-"),
            ".txt"),
        p.DebugString()));
  }

  LOG(INFO) << "Sent " << num_packets << " packets in "
            << (absl::Now() - start_time) << ".";
  return absl::OkStatus();
}

void PrettyPrintDistribution(
    const TestConfiguration& config, const TestInputOutput& test,
    const TestData& test_data, absl::Span<const Member> members,
    const absl::flat_hash_map<int, int>& num_packets_per_port) {
  LOG(INFO) << "Results for " << DescribeTestConfig(config) << ":";
  LOG(INFO) << "- received " << test.output.size() << " packets";
  LOG(INFO) << "- observed distribution was:"
            << DescribeDistribution(test_data.total_packets_sent, members,
                                    num_packets_per_port,
                                    /*expect_single_port=*/false);
  LOG(INFO) << "Number of sent packets:               "
            << test_data.total_packets_sent;
  LOG(INFO) << "Number of received packets (valid):   "
            << test_data.total_packets_received;
  LOG(INFO) << "Number of received packets (invalid): "
            << test_data.total_invalid_packets_received;
}

}  // namespace

void WatchPortTestFixture::SetUp() {
  MirrorTestbedFixture::SetUp();
  thinkit::MirrorTestbed& testbed = GetMirrorTestbed();

  // Push gnmi config to the sut and control switch.
  const std::string& gnmi_config = GetGnmiConfig();
  ASSERT_OK(
      testbed.Environment().StoreTestArtifact("gnmi_config.txt", gnmi_config));
  ASSERT_OK(pins_test::PushGnmiConfig(testbed.Sut(), gnmi_config));
  ASSERT_OK(pins_test::PushGnmiConfig(testbed.ControlSwitch(), gnmi_config));

  ASSERT_OK_AND_ASSIGN(sut_p4_session_,
                       pdpi::P4RuntimeSession::Create(testbed.Sut()));
  ASSERT_OK_AND_ASSIGN(control_p4_session_,
                       pdpi::P4RuntimeSession::Create(testbed.ControlSwitch()));

  ASSERT_OK(testbed.Environment().StoreTestArtifact("p4info.pb.txt",
                                                    GetP4Info().DebugString()));

  // Setup SUT & control switch.
  ASSERT_OK(SetUpSut(*sut_p4_session_, GetP4Info(), GetIrP4Info(), kVrfId));
  ASSERT_OK(
      SetUpControlSwitch(*control_p4_session_, GetP4Info(), GetIrP4Info()));

  // Start the receiver thread for control switch to listen for packets from
  // SUT, this thread is terminated in the TearDown.
  receive_packet_thread_ = std::thread([&]() {
    p4::v1::StreamMessageResponse pi_response;
    while (control_p4_session_->StreamChannelRead(pi_response)) {
      absl::MutexLock lock(&test_data_.mutex);
      sai::StreamMessageResponse pd_response;
      ASSERT_OK(pdpi::PiStreamMessageResponseToPd(GetIrP4Info(), pi_response,
                                                  &pd_response))
          << " PacketIn PI to PD failed: ";
      ASSERT_TRUE(pd_response.has_packet())
          << " Received unexpected stream message for packet in: "
          << pd_response.DebugString();
      absl::string_view raw_packet = pd_response.packet().payload();
      gpins::Packet packet;
      packet.set_port(pd_response.packet().metadata().ingress_port());
      packet.set_hex(absl::BytesToHexString(raw_packet));
      *packet.mutable_parsed() = packetlib::ParsePacket(raw_packet);
      std::string key = packet.parsed().payload();
      if (test_data_.input_output_per_packet.contains(key)) {
        test_data_.input_output_per_packet[key].output.push_back(packet);
        test_data_.total_packets_received += 1;
      } else {
        ASSERT_OK(testbed.Environment().AppendToTestArtifact(
            "control_unexpected_packet_ins.pb.txt",
            absl::StrCat(packet.DebugString(), "\n")));
        test_data_.total_invalid_packets_received += 1;
      }
    }
  });
}

void WatchPortTestFixture::TearDown() {
  // Clear table entries.
  if (sut_p4_session_ != nullptr) {
    EXPECT_OK(pdpi::ClearTableEntries(sut_p4_session_.get(), GetIrP4Info()));
    sut_p4_session_->TryCancel();
  }
  // Stop RPC sessions.
  if (control_p4_session_ != nullptr) {
    EXPECT_OK(
        pdpi::ClearTableEntries(control_p4_session_.get(), GetIrP4Info()));
    control_p4_session_->TryCancel();
  }
  if (receive_packet_thread_.joinable()) {
    receive_packet_thread_.join();
  }
  thinkit::MirrorTestbedFixture::TearDown();
}

namespace {

// Verifies basic WCMP behavior by programming a group with multiple members
// with random weights and ensuring that all members receive some part of
// the sent traffic.
TEST_P(WatchPortTestFixture, VerifyBasicWcmpPacketDistribution) {
  thinkit::TestEnvironment& environment = GetMirrorTestbed().Environment();
  // Validate that we have enough ports for the test.
  ASSERT_TRUE(GetPortIds().has_value())
      << "Controller port ids (required) but not provided.";
  ASSERT_GE((*GetPortIds()).size(), kNumWcmpMembersForTest);
  ASSERT_OK_AND_ASSIGN(std::vector<Member> members,
                       CreateMembers(*GetPortIds()));

  // Programs the required router interfaces, nexthops for wcmp group.
  ASSERT_OK(gpins::ProgramNextHops(environment, *sut_p4_session_, GetIrP4Info(),
                                   members));
  ASSERT_OK(gpins::ProgramGroupWithMembers(environment, *sut_p4_session_,
                                           GetIrP4Info(), kGroupId, members,
                                           p4::v1::Update::INSERT))
      << "Failed to program WCMP group: ";

  // Program default routing for all packets on SUT.
  ASSERT_OK(ProgramDefaultRoutes(*sut_p4_session_, GetIrP4Info(), kVrfId));

  // TODO: Revisit for newer chipsets.
  // Rescale the member weights (temp workaround) to what would have been
  // programmed by the hardware.
  RescaleMemberWeights(members);

  // Generate test configuration, pick any field (IP_SRC) used by hashing to
  // vary for every packet so that it gets sent to all the members.
  TestConfiguration test_config = {
      .field = PacketField::kIpSrc,
      .ipv4 = true,
      .encapped = false,
      .inner_ipv4 = false,
      .decap = false,
  };
  ASSERT_TRUE(IsValidTestConfiguration(test_config));

  // Create test data entry.
  std::string test_data_key = TestConfigurationToPayload(test_config);
  {
    absl::MutexLock lock(&test_data_.mutex);
    test_data_.input_output_per_packet[test_data_key] = TestInputOutput{
        .config = test_config,
    };
  }

  // Send 5000 packets and check for packet distribution.
  ASSERT_OK(SendNPacketsToSut(kNumTestPackets, test_config, members,
                              GetPortIds().value(), GetIrP4Info(),
                              *control_p4_session_, environment));
  test_data_.total_packets_sent = kNumTestPackets;

  // Wait for packets from the SUT to arrive.
  absl::SleepFor(kDurationToWaitForPackets);

  // For the test configuration, check the output distribution.
  {
    absl::MutexLock lock(&test_data_.mutex);
    const TestInputOutput& test =
        test_data_.input_output_per_packet[test_data_key];
    EXPECT_EQ(test.output.size(), test_data_.total_packets_sent)
        << "Mismatch in expected: " << test_data_.total_packets_sent
        << " and actual: " << test.output.size() << "packets received for "
        << DescribeTestConfig(test_config);

    // Count number of packets received per port
    absl::flat_hash_map<int, int> num_packets_per_port;
    for (const auto& output : test.output) {
      ASSERT_OK_AND_ASSIGN(int out_port,
                           pdpi::DecimalStringToInt(output.port()));
      num_packets_per_port[out_port] += 1;
    }
    absl::flat_hash_set<int> expected_member_ports;
    for (const auto& member : members) {
      expected_member_ports.insert(member.port);
    }
    ASSERT_OK(VerifyGroupMembersFromP4Read(*sut_p4_session_, GetIrP4Info(),
                                           kGroupId, members));
    ASSERT_OK(VerifyGroupMembersFromReceiveTraffic(num_packets_per_port,
                                                   expected_member_ports));
    PrettyPrintDistribution(test_config, test, test_data_, members,
                            num_packets_per_port);
  }
}

// TODO: Bring down/up APG member and verify traffic is distributed only to the
// up ports.
TEST_P(WatchPortTestFixture, VerifyBasicWatchPortAction){};

// TODO: Bring down APG member (when in critical state) and verify traffic is
// distributed only to the up ports.
TEST_P(WatchPortTestFixture, VerifyWatchPortActionInCriticalState){};

// TODO: Bring up/down the only APG member and verify traffic is distributed or
// dropped.
TEST_P(WatchPortTestFixture, VerifyWatchPortActionForSingleMember){};

// TODO: Modify APG member and verify traffic is distributed accordingly.
TEST_P(WatchPortTestFixture, VerifyWatchPortActionForMemberModify){};

// TODO: Add APG member whose watch port is down and verify traffic distribution
// when port is down/up.
TEST_P(WatchPortTestFixture, VerifyWatchPortActionForDownPortMemberInsert){};

}  // namespace
}  // namespace gpins

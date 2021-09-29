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

#include <algorithm>
#include <cctype>
#include <memory>
#include <ostream>
#include <vector>

#include "absl/algorithm/algorithm.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
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
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/pd.h"
#include "p4_pdpi/string_encodings/decimal_string.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/group_programming_util.h"
#include "tests/forwarding/packet_test_util.h"
#include "tests/forwarding/util.h"
#include "thinkit/mirror_testbed_fixture.h"

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

constexpr absl::Duration kDurationToWaitForPacketsFromSut = absl::Seconds(30);

std::vector<PacketField> AllFields() {
  return {
      PacketField::kEthernetSrc,
      PacketField::kEthernetDst,
      PacketField::kIpSrc,
      PacketField::kIpDst,
      PacketField::kHopLimit,
      PacketField::kDscp,
      PacketField::kFlowLabelLower16,
      PacketField::kFlowLabelUpper4,
      PacketField::kInnerIpSrc,
      PacketField::kInnerIpDst,
      PacketField::kInnerHopLimit,
      PacketField::kInnerDscp,
      PacketField::kInnerFlowLabelLower16,
      PacketField::kInnerFlowLabelUpper4,
      PacketField::kL4SrcPort,
      PacketField::kL4DstPort,
      PacketField::kInputPort,
  };
}

// Returns true if packets generated for this config should be load-balanced.
bool PacketsShouldBeHashed(const TestConfiguration& config) {
  switch (config.field) {
    case PacketField::kIpSrc:
    case PacketField::kIpDst:
    case PacketField::kFlowLabelLower16:
      return !config.encapped;
    case PacketField::kInnerIpSrc:
    case PacketField::kInnerIpDst:
    case PacketField::kInnerFlowLabelLower16:
    case PacketField::kL4SrcPort:
    case PacketField::kL4DstPort:
      return true;
    default:
      return false;
  }
}

// Number of Wcmp members in a group for this test.
constexpr int kNumWcmpMembersForTest = 3;

constexpr absl::string_view kSetVrfTableEntry = R"pb(
  acl_pre_ingress_table_entry {
    match {}
    action { set_vrf { vrf_id: "vrf-80" } }
    priority: 1129
  })pb";

constexpr absl::string_view kIpv4DefaultRouteEntry = R"pb(
  ipv4_table_entry {
    match { vrf_id: "vrf-80" }
    action { set_wcmp_group_id { wcmp_group_id: "" } }
  }
)pb";

constexpr absl::string_view kIpv6DefaultRouteEntry = R"pb(
  ipv6_table_entry {
    match { vrf_id: "vrf-80" }
    action { set_wcmp_group_id { wcmp_group_id: "" } }
  })pb";

constexpr absl::string_view kDstMacClassifier = R"pb(
  l3_admit_table_entry {
    match {}
    action { admit_to_l3 {} }
    priority: 2070
  })pb";

// Number of extra packets to send.  Up to this many packets can then be dropped
// and we can still perform the statistical test.
constexpr int kNumExtraPackets = 10;

// Returns the number of packets to send.
int GetNumberOfPackets(bool should_be_hashed) {
  // Current max packets is set for a max sum of weights 15, error rate of 10%
  // and pvalue of 0.001.
  if (should_be_hashed) return 7586;
  return 10;
}
int GetNumberOfPackets(TestConfiguration config) {
  return GetNumberOfPackets(PacketsShouldBeHashed(config));
}

absl::Status SetUpSut(pdpi::P4RuntimeSession* const p4_session,
                      const p4::config::v1::P4Info& p4info) {
  RETURN_IF_ERROR(
      pdpi::SetForwardingPipelineConfig(
          p4_session,
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4info))
          .SetPrepend()
      << "Failed to push P4Info for Sut: ";
  RETURN_IF_ERROR(pdpi::ClearTableEntries(p4_session));
  return absl::OkStatus();
}

absl::Status SetUpControlSwitch(pdpi::P4RuntimeSession* const p4_session,
                                const p4::config::v1::P4Info& p4info) {
  RETURN_IF_ERROR(
      pdpi::SetForwardingPipelineConfig(
          p4_session,
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4info))
          .SetPrepend()
      << "Failed to push P4Info for Control switch: ";
  RETURN_IF_ERROR(pdpi::ClearTableEntries(p4_session));
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

// Returns the set of entities required for the hashing test.
absl::Status ProgramHashingEntities(thinkit::TestEnvironment& test_environment,
                                    pdpi::P4RuntimeSession& session,
                                    const pdpi::IrP4Info& ir_p4info,
                                    std::vector<gpins::GroupMember>& members) {
  RETURN_IF_ERROR(
      gpins::ProgramNextHops(test_environment, session, ir_p4info, members));

  RETURN_IF_ERROR(gpins::ProgramGroupWithMembers(test_environment, session,
                                                 ir_p4info, "group-1", members,
                                                 p4::v1::Update::INSERT))
          .SetPrepend()
      << "Failed to program WCMP group: ";

  std::vector<p4::v1::TableEntry> pi_entries;

  // Set default VRF for all packets.
  ASSIGN_OR_RETURN(
      p4::v1::TableEntry pi_entry,
      pdpi::PdTableEntryToPi(ir_p4info, gutil::ParseProtoOrDie<sai::TableEntry>(
                                            kSetVrfTableEntry)));
  pi_entries.push_back(pi_entry);

  // Add flows to allow destination mac variations.
  auto l3_dst_mac_classifier =
      gutil::ParseProtoOrDie<sai::TableEntry>(kDstMacClassifier);
  for (int i = 0; i < GetNumberOfPackets(/*should_be_hashed=*/false); i++) {
    netaddr::MacAddress netaddr_mac(GetIthDstMac(i));

    l3_dst_mac_classifier.mutable_l3_admit_table_entry()
        ->mutable_match()
        ->mutable_dst_mac()
        ->set_value(netaddr_mac.ToString());
    l3_dst_mac_classifier.mutable_l3_admit_table_entry()
        ->mutable_match()
        ->mutable_dst_mac()
        ->set_mask("ff:ff:ff:ff:ff:ff");

    ASSIGN_OR_RETURN(
        pi_entry, pdpi::PdTableEntryToPi(ir_p4info, l3_dst_mac_classifier),
        _.SetPrepend() << "Failed in PD table conversion to PI, entry: "
                       << l3_dst_mac_classifier.DebugString() << " error: ");
    pi_entries.push_back(pi_entry);
  }

  // Add minimal set of flows to allow forwarding.
  auto ipv4_fallback =
      gutil::ParseProtoOrDie<sai::TableEntry>(kIpv4DefaultRouteEntry);
  ipv4_fallback.mutable_ipv4_table_entry()
      ->mutable_action()
      ->mutable_set_wcmp_group_id()
      ->set_wcmp_group_id("group-1");
  ASSIGN_OR_RETURN(pi_entry, pdpi::PdTableEntryToPi(ir_p4info, ipv4_fallback),
                   _.SetPrepend()
                       << "Failed in PD table conversion to PI, entry: "
                       << ipv4_fallback.DebugString() << " error: ");
  pi_entries.push_back(pi_entry);

  auto ipv6_fallback =
      gutil::ParseProtoOrDie<sai::TableEntry>(kIpv6DefaultRouteEntry);
  ipv6_fallback.mutable_ipv6_table_entry()
      ->mutable_action()
      ->mutable_set_wcmp_group_id()
      ->set_wcmp_group_id("group-1");
  ASSIGN_OR_RETURN(pi_entry, pdpi::PdTableEntryToPi(ir_p4info, ipv6_fallback),
                   _.SetPrepend()
                       << "Failed in PD table conversion to PI, entry: "
                       << ipv6_fallback.DebugString() << " error: ");
  pi_entries.push_back(pi_entry);

  return pdpi::InstallPiTableEntries(&session, ir_p4info, pi_entries);
}

// Generate all possible test configurations, send packets for every config, and
// check that the observed distribution is correct.
TEST_P(HashingTestFixture, SendPacketsToWcmpGroupsAndCheckDistribution) {
  LOG(INFO) << "Starting actual test";

  thinkit::MirrorTestbed& testbed =
      GetParam().mirror_testbed->GetMirrorTestbed();

  testbed.Environment().SetTestCaseIDs(
      {"789dad22-96d1-4550-8acb-d42c1f69ca21",
       "fdaa1b1e-67a3-497f-aa62-fd62d711c415"});

  absl::Span<const int> orion_port_ids = GetParam().port_ids;
  ASSERT_GE(orion_port_ids.size(), kNumWcmpMembersForTest);

  // The port on which we input all dataplane test packets.
  const int ingress_port = orion_port_ids[0];

  // Setup SUT & control switch.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> sut_p4_session,
                       pdpi::P4RuntimeSession::Create(testbed.Sut()));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> control_p4_session,
      pdpi::P4RuntimeSession::Create(testbed.ControlSwitch()));

  const std::string& gnmi_config = GetParam().gnmi_config;
  ASSERT_OK(
      testbed.Environment().StoreTestArtifact("gnmi_config.txt", gnmi_config));
  ASSERT_OK(pins_test::PushGnmiConfig(testbed.Sut(), gnmi_config));
  ASSERT_OK(pins_test::PushGnmiConfig(testbed.ControlSwitch(), gnmi_config));

  // Obtain P4Info for SAI P4 program.
  const p4::config::v1::P4Info p4info =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);
  ASSERT_OK(testbed.Environment().StoreTestArtifact("p4info.pb.txt",
                                                    p4info.DebugString()));
  ASSERT_OK_AND_ASSIGN(const pdpi::IrP4Info ir_p4info,
                       pdpi::CreateIrP4Info(p4info));
  ASSERT_OK(SetUpSut(sut_p4_session.get(), p4info));
  ASSERT_OK(SetUpControlSwitch(control_p4_session.get(), p4info));

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
      if (test_data.input_output_per_packet.contains(key)) {
        test_data.input_output_per_packet[key].output.push_back(packet);
        test_data.total_packets_received += 1;
      } else {
        if ((testbed.Environment().MaskKnownFailures() ||
             absl::GetFlag(FLAGS_ignore_router_solicitation_packets)) &&
            packet.parsed().headers().size() == 3 &&
            packet.parsed().headers(2).icmp_header().type() == "0x85") {
          ASSERT_OK(testbed.Environment().AppendToTestArtifact(
              "control_unexpected_packet_ins.pb.txt",
              absl::StrCat(packet.DebugString(), "\n")));

        } else {
          test_data.total_invalid_packets_received += 1;
        }
      }
    }
  };
  std::thread receive_packet_fiber(ReceivePacketFiber);

  // Iterate over 3 sets of random weights for 3 ports.
  for (int iter = 0; iter < 3; iter++) {
    std::vector<GroupMember> members(kNumWcmpMembersForTest);
    for (int i = 0; i < kNumWcmpMembersForTest; i++) {
      members[i] = gpins::GroupMember{.weight = 0, .port = orion_port_ids[i]};
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
                           gpins::GenerateNRandomWeights(kNumWcmpMembersForTest,
                                                         /*total_weight=*/30));
    }
    for (int i = 0; i < members.size(); i++) {
      members[i].weight = weights[i];
    }

    ASSERT_OK(ProgramHashingEntities(testbed.Environment(), *sut_p4_session,
                                     ir_p4info, members));

    // Apply the member weights tweak if applicable.
    if (GetParam().tweak_member_weight.has_value()) {
      for (int i = 0; i < members.size(); i++) {
        members[i].weight =
            (*GetParam().tweak_member_weight)(members[i].weight);
        LOG(INFO) << "Rescaling member id: " << members[i].port
                  << " from weight: " << weights[i]
                  << " to new weight: " << members[i].weight;
      }
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
              if (field == PacketField::kFlowLabelUpper4 ||
                  field == PacketField::kInnerFlowLabelUpper4)
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
                test_data.input_output_per_packet.insert({key, inout});
              }

              // Send packets to the switch.
              std::string packet_log = "";

              for (int idx = 0;
                   idx < GetNumberOfPackets(config) + kNumExtraPackets; idx++) {
                // Rate limit to 500 packets per second.
                auto now = absl::Now();
                auto earliest_send_time =
                    start + (total_packets * absl::Seconds(1) / 500.0);
                if (earliest_send_time > now) {
                  absl::SleepFor(earliest_send_time - now);
                }

                int port = ingress_port;
                if (field == PacketField::kInputPort) {
                  port = orion_port_ids[idx % members.size()];
                }

                ASSERT_OK_AND_ASSIGN(auto packet,
                                     gpins::GenerateIthPacket(config, idx));
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
      EXPECT_OK(pdpi::ClearTableEntries(sut_p4_session.get()));
      LOG(INFO) << "Cleared table entries on SUT in " << (absl::Now() - start);
    }

    // For each test configuration, check the output distribution.
    {
      absl::flat_hash_set<int> expected_ports;
      for (const auto& member : members) {
        expected_ports.insert(member.port);
      }
      absl::MutexLock lock(&test_data.mutex);
      for (const auto& config : configs) {
        const auto& key = TestConfigurationToPayload(config);
        const TestInputOutput& test = test_data.input_output_per_packet[key];
        auto n_packets = GetNumberOfPackets(config);
        EXPECT_GE(test.output.size(), n_packets)
            << "Not enough packets received for " << DescribeTestConfig(config);

        // Proceed with the actual number of packets received
        n_packets = test.output.size();
        if (n_packets == 0) continue;

        // Count packets per port
        absl::flat_hash_map<int, int> num_packets_per_port;
        for (const auto& output : test.output) {
          ASSERT_OK_AND_ASSIGN(int out_port,
                               pdpi::DecimalStringToUint32(output.port()));
          num_packets_per_port[out_port] += 1;
        }
        ASSERT_OK(VerifyGroupMembersFromReceiveTraffic(num_packets_per_port,
                                                       expected_ports));

        LOG(INFO) << "Results for " << DescribeTestConfig(config) << ":";
        LOG(INFO) << "- received " << test.output.size() << " packets";
        LOG(INFO) << "- observed distribution was:"
                  << DescribeDistribution(GetNumberOfPackets(config), members,
                                          num_packets_per_port,
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
            double actual_count = num_packets_per_port[member.port];
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
          if ((config.field != PacketField::kL4SrcPort &&
               config.field != PacketField::kL4DstPort) ||
              !absl::GetFlag(FLAGS_ignore_l4_port_hashing)) {
            EXPECT_GT(pvalue, 0.001)
                << "For config " << DescribeTestConfig(config) << ": "
                << "The p-value is small enough that we reject the "
                   "null-hypothesis "
                   "(H_0 = 'The switch distribution is correct'), and "
                   "instead "
                   "have strong evidence that switch produces the wrong "
                   "distribution:"
                << DescribeDistribution(GetNumberOfPackets(config), members,
                                        num_packets_per_port,
                                        /*expect_one_port=*/false);
          }
        } else {
          LOG(INFO) << "- packets were forwarded to "
                    << num_packets_per_port.size() << " ports";
          // Expect all packets to be forwarded to the same port.
          EXPECT_EQ(num_packets_per_port.size(), 1)
              << "Expected the test configuration " << std::endl
              << DescribeTestConfig(config) << std::endl
              << "to not influence the hash, and thus all packets should be "
                 "forwarded on a single port.  Instead, the following was "
                 "observed: "
              << DescribeDistribution(GetNumberOfPackets(config), members,
                                      num_packets_per_port,
                                      /*expect_one_port=*/true);
        }
      }

      LOG(INFO) << "Number of sent packets:               " << total_packets;
      LOG(INFO) << "Number of received packets (valid):   "
                << test_data.total_packets_received;
      LOG(INFO) << "Number of received packets (invalid): "
                << test_data.total_invalid_packets_received;

      // Clear TestData so that it can used by the next set of members.
      test_data.input_output_per_packet.clear();
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

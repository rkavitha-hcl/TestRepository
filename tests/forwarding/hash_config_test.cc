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

#include "tests/forwarding/hash_config_test.h"

#include <complex>
#include <memory>
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/gnmi/gnmi_helper.h"
#include "lib/validator/validator_lib.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/netaddr/mac_address.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/pd.h"
#include "p4_pdpi/string_encodings/decimal_string.h"
#include "re2/re2.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/group_programming_util.h"
#include "tests/forwarding/packet_test_util.h"
#include "tests/forwarding/util.h"
#include "tests/lib/switch_test_setup_helpers.h"
#include "tests/thinkit_sanity_tests.h"
#include "thinkit/mirror_testbed_fixture.h"
#include "thinkit/test_environment.h"

namespace pins_test {

absl::node_hash_map<std::string, HashConfigTest::TestData>*
    HashConfigTest::original_p4info_test_data_ = nullptr;

namespace {

using ::gpins::PacketField;
using ::gpins::TestConfiguration;
using ::gutil::EqualsProto;
using ::gutil::ParseProtoOrDie;
using ::gutil::ParseTextProto;
using ::testing::Contains;
using ::testing::Key;
using ::testing::Matches;
using ::testing::Not;
using ::testing::UnorderedElementsAreArray;

// The minimum number of ports needed to perform the test.
constexpr int kMinimumMembersForTest = 3;

// The number of packets to generate for each test config.
constexpr int kNumPackets = 100;

// Average interval between packet injections.
constexpr absl::Duration kPacketInterval = absl::Milliseconds(10);  // 100pps

// P4TableEntry templates needed to set up hashing.
constexpr absl::string_view kAddVrfTableEntry = R"pb(
  vrf_table_entry {
    match { vrf_id: "vrf-80" }
    action { no_action {} }
  })pb";

constexpr absl::string_view kSetVrfTableEntry = R"pb(
  acl_pre_ingress_table_entry {
    match {}
    action { set_vrf { vrf_id: "vrf-80" } }
    priority: 1129
  })pb";

constexpr absl::string_view kIpv4DefaultRouteEntry = R"pb(
  ipv4_table_entry {
    match { vrf_id: "vrf-80" }
    action { set_wcmp_group_id { wcmp_group_id: "group-1" } }
  })pb";

constexpr absl::string_view kIpv6DefaultRouteEntry = R"pb(
  ipv6_table_entry {
    match { vrf_id: "vrf-80" }
    action { set_wcmp_group_id { wcmp_group_id: "group-1" } }
  })pb";

// Set the dst_mac value manually.
constexpr absl::string_view kDstMacClassifier = R"pb(
  l3_admit_table_entry {
    match { dst_mac { mask: "ff:ff:ff:ff:ff:ff" } }
    action { admit_to_l3 {} }
    priority: 2070
  })pb";

// Return the list of all packet TestConfigurations to be tested. Each
// TestConfiguration should result in a hash difference.
const absl::btree_map<std::string, TestConfiguration>& TestConfigs() {
  static const auto* const kTestConfigs = new absl::btree_map<
      std::string, TestConfiguration>({
      {"IPv4DiffIpSrc", {.field = PacketField::kIpSrc, .ipv4 = true}},
      {"IPv4DiffIpDst", {.field = PacketField::kIpDst, .ipv4 = true}},
      {"IPv6DiffIpSrc", {.field = PacketField::kIpSrc, .ipv4 = false}},
      {"IPv6DiffIpDst", {.field = PacketField::kIpDst, .ipv4 = false}},
      {"IPv6DiffFlowLabelLower16",
       {.field = PacketField::kFlowLabelLower16, .ipv4 = false}},
      {"IPv4DiffL4SrcPort", {.field = PacketField::kL4SrcPort, .ipv4 = true}},
      {"IPv4DiffL4DstPort", {.field = PacketField::kL4DstPort, .ipv4 = true}},
      {"IPv6DiffL4SrcPort", {.field = PacketField::kL4SrcPort, .ipv4 = false}},
      {"IPv6DiffL4DstPort", {.field = PacketField::kL4DstPort, .ipv4 = false}},
  });
  return *kTestConfigs;
}

// Return the list of all TestConfig() names.
const std::vector<std::string>& TestConfigNames() {
  static const auto* const kConfigNames = []() {
    auto config_names = new std::vector<std::string>();
    for (const auto& [config_name, test_config] : TestConfigs()) {
      config_names->push_back(config_name);
    }
    return config_names;
  }();
  return *kConfigNames;
}

// Set the payload for a HashConfigTest packet that contains an identifier
// and the packet index.
void SetPayload(packetlib::Packet& packet, int index) {
  packet.set_payload(
      absl::Substitute("HashAlgPacket($0): $1", index, packet.payload()));
}

// Return the index of a HashConfigTest packet or an error if parsing fails.
absl::StatusOr<int> GetPacketIndex(const packetlib::Packet& packet) {
  static const LazyRE2 kIndexRegex = {R"re(^HashAlgPacket\(([0-9]*)\))re"};
  int index;
  if (!RE2::PartialMatch(packet.payload(), *kIndexRegex, &index)) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Packet payload does not match expected format: "
              "HashAlgPacket(<index>): <original_payload>. ";
  }
  return index;
}

// Log a set of packets as a single artifact for debugging.
void LogPackets(thinkit::TestEnvironment& environment,
                const std::vector<packetlib::Packet>& packets,
                absl::string_view artifact_name) {
  std::string packet_log;
  for (const auto& packet : packets) {
    absl::StrAppend(&packet_log, packet.ShortDebugString(), "\n");
  }
  ASSERT_OK(environment.StoreTestArtifact(absl::StrCat(artifact_name, ".txt"),
                                          packet_log));
}

// This class facilitates performing an action at an average rate no faster than
// the provided interval. To use this class, call Wait() before each
// rate-limited action.
class RateLimit {
 public:
  explicit RateLimit(absl::Duration interval)
      : interval_(interval), deadline_(absl::Now()) {}

  void Wait() {
    if (absl::Now() < deadline_) absl::SleepFor(deadline_ - absl::Now());
    deadline_ += interval_;
  }

 private:
  const absl::Duration interval_;
  absl::Time deadline_;
};

// Installs the set of entities required for the hashing test.
void ProgramHashingEntities(thinkit::MirrorTestbed& testbed,
                            const p4::config::v1::P4Info& p4info,
                            const absl::btree_set<int>& port_ids) {
  std::vector<gpins::GroupMember> members;
  for (int port_id : port_ids) {
    members.push_back({.weight = 1, .port = port_id});
  }
  ASSERT_OK_AND_ASSIGN(auto ir_p4info, pdpi::CreateIrP4Info(p4info));
  ASSERT_OK_AND_ASSIGN(
      auto session, pins_test::ConfigureSwitchAndReturnP4RuntimeSession(
                        testbed.Sut(), /*gnmi_config=*/std::nullopt, p4info));
  ASSERT_OK(gpins::ProgramNextHops(testbed.Environment(), *session, ir_p4info,
                                   members));

  ASSERT_OK(gpins::ProgramGroupWithMembers(testbed.Environment(), *session,
                                           ir_p4info, "group-1", members,
                                           p4::v1::Update::INSERT))
      << "Failed to program WCMP group.";

  std::vector<p4::v1::TableEntry> pi_entries;
  // Create default VRF.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::TableEntry pi_entry,
      pdpi::PdTableEntryToPi(
          ir_p4info, ParseProtoOrDie<sai::TableEntry>(kAddVrfTableEntry)));
  pi_entries.push_back(pi_entry);

  // Set default VRF for all packets.
  ASSERT_OK_AND_ASSIGN(
      pi_entry,
      pdpi::PdTableEntryToPi(
          ir_p4info, ParseProtoOrDie<sai::TableEntry>(kSetVrfTableEntry)));
  pi_entries.push_back(pi_entry);

  // Add flows to allow destination mac variations.
  auto l3_dst_mac_classifier =
      ParseProtoOrDie<sai::TableEntry>(kDstMacClassifier);
  l3_dst_mac_classifier.mutable_l3_admit_table_entry()
      ->mutable_match()
      ->mutable_dst_mac()
      ->set_value(gpins::GetIthDstMac(0).ToString());
  ASSERT_OK_AND_ASSIGN(
      pi_entry, pdpi::PdTableEntryToPi(ir_p4info, l3_dst_mac_classifier));
  pi_entries.push_back(pi_entry);

  // Add minimal set of flows to allow forwarding.
  ASSERT_OK_AND_ASSIGN(
      pi_entry,
      pdpi::PdTableEntryToPi(
          ir_p4info, ParseProtoOrDie<sai::TableEntry>(kIpv4DefaultRouteEntry)));
  pi_entries.push_back(pi_entry);

  ASSERT_OK_AND_ASSIGN(
      pi_entry,
      pdpi::PdTableEntryToPi(
          ir_p4info, ParseProtoOrDie<sai::TableEntry>(kIpv6DefaultRouteEntry)));
  pi_entries.push_back(pi_entry);

  ASSERT_OK(pdpi::InstallPiTableEntries(session.get(), ir_p4info, pi_entries));
}

// Initialize the testbed for the test.
//   Push gNMI config.
//   Add the trap rule to the control switch.
void InitializeTestbed(thinkit::MirrorTestbed& testbed,
                       const std::string& gnmi_config,
                       const p4::config::v1::P4Info& p4info) {
  // Push gNMI configuration to the SUT & control switch.
  ASSERT_OK(
      testbed.Environment().StoreTestArtifact("gnmi_config.txt", gnmi_config));
  ASSERT_OK(PushGnmiConfig(testbed.Sut(), gnmi_config));
  ASSERT_OK(PushGnmiConfig(testbed.ControlSwitch(), gnmi_config));
  ASSERT_OK(WaitForGnmiPortIdConvergence(testbed.Sut(), gnmi_config,
                                         /*timeout=*/absl::Minutes(3)));
  ASSERT_OK(WaitForGnmiPortIdConvergence(testbed.ControlSwitch(), gnmi_config,
                                         /*timeout=*/absl::Minutes(3)));

  // Wait for ports to come up before the test. We don't need all the ports to
  // be up, but it helps with reproducibility. We're using a short timeout (1
  // minute) so the impact is small if the testbed doesn't bring up every port.
  if (auto all_interfaces_up_status = WaitForCondition(
          AllPortsUp, absl::Minutes(1), testbed.Sut(), /*with_healthz=*/false);
      !all_interfaces_up_status.ok()) {
    LOG(WARNING) << "Some ports are down at the start of the test. Continuing "
                 << "with only the UP ports. " << all_interfaces_up_status;
  }

  // Setup control switch P4 state.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> control_p4_session,
      pins_test::ConfigureSwitchAndReturnP4RuntimeSession(
          testbed.ControlSwitch(), /*gnmi_config=*/std::nullopt, p4info));

  // Trap all packets on control switch.
  ASSERT_OK_AND_ASSIGN(pdpi::IrP4Info ir_p4info, pdpi::CreateIrP4Info(p4info));
  ASSERT_OK_AND_ASSIGN(
      p4::v1::TableEntry punt_all_pi_entry,
      pdpi::PdTableEntryToPi(
          ir_p4info,
          ParseProtoOrDie<sai::TableEntry>(
              R"pb(
                acl_ingress_table_entry {
                  match {}                                  # Wildcard match.
                  action { acl_trap { qos_queue: "0x1" } }  # Action: punt.
                  priority: 1                               # Highest priority.
                  # TODO: Remove once GPINs V13 is
                  # deprecated; only needed for backwards compatibility.
                  meter_config {
                    bytes_per_second: 987654321  # ~ 1 GB
                    burst_bytes: 987654321       # ~ 1 GB
                  }
                }
              )pb")));
  ASSERT_OK(
      pdpi::InstallPiTableEntry(control_p4_session.get(), punt_all_pi_entry));
}

// Receive and record a single packet.
void ReceivePacket(thinkit::MirrorTestbed* testbed,
                   const pdpi::IrP4Info* ir_p4info,
                   p4::v1::StreamMessageResponse pi_response,
                   HashConfigTest::TestData* test_data) {
  sai::StreamMessageResponse pd_response;
  ASSERT_OK(
      pdpi::PiStreamMessageResponseToPd(*ir_p4info, pi_response, &pd_response))
      << " PacketIn PI to PD failed: ";
  if (!pd_response.has_packet()) {
    LOG(WARNING) << "Ignoring unexpected stream message for packet in: "
                 << pd_response.DebugString();
  }

  absl::string_view raw_packet = pd_response.packet().payload();
  packetlib::Packet packet = packetlib::ParsePacket(raw_packet);
  test_data->AddPacket(pd_response.packet().metadata().target_egress_port(),
                       std::move(packet));
}

// Thread function to receive and record test packets.
void ReceivePacketsUntilStreamIsClosed(
    thinkit::MirrorTestbed* testbed, const pdpi::IrP4Info* ir_p4info,
    pdpi::P4RuntimeSession* control_p4_session,
    HashConfigTest::TestData* test_data) {
  p4::v1::StreamMessageResponse pi_response;
  // The only way to break out of this loop is for the stream channel to
  // be closed. gRPC does not support selecting on both stream Read and
  // fiber Cancel.
  while (control_p4_session->StreamChannelRead(pi_response)) {
    ReceivePacket(testbed, ir_p4info, pi_response, test_data);
  }
}

// Send a test packet to the SUT.
void SendPacket(const pdpi::IrP4Info& ir_p4info, packetlib::Packet packet,
                pdpi::P4RuntimeSession& control_p4_session, int ingress_port) {
  SCOPED_TRACE(
      absl::StrCat("Failed to inject packet ", packet.ShortDebugString()));
  ASSERT_OK_AND_ASSIGN(std::string raw_packet, SerializePacket(packet));
  ASSERT_OK_AND_ASSIGN(std::string port_string,
                       pdpi::IntToDecimalString(ingress_port));
  ASSERT_OK(gpins::InjectEgressPacket(port_string, raw_packet, ir_p4info,
                                      &control_p4_session,
                                      /*packet_delay=*/std::nullopt));
}

// Send test packets to the SUT. Packets are generated based on the test config.
void SendPackets(const pdpi::IrP4Info& ir_p4info,
                 const TestConfiguration& test_config,
                 pdpi::P4RuntimeSession& control_p4_session, int ingress_port,
                 std::vector<packetlib::Packet>& injected_packets) {
  RateLimit rate_limit(kPacketInterval);
  // Try to generate one packet first to see if the config is valid.
  {
    ASSERT_OK_AND_ASSIGN(packetlib::Packet packet,
                         gpins::GenerateIthPacket(test_config, 0));
    ASSERT_OK(SerializePacket(packet).status())
        << "Failed to generate raw packet for " << packet.ShortDebugString();
  }
  for (int i = 0; i < kNumPackets; ++i) {
    rate_limit.Wait();  // Inject based on the rate limit.
    ASSERT_OK_AND_ASSIGN(packetlib::Packet packet,
                         gpins::GenerateIthPacket(test_config, i));
    SetPayload(packet, i);
    injected_packets.push_back(packet);
    // Don't check errors from SendPacket. Continue sending packets.
    SendPacket(ir_p4info, packet, control_p4_session, ingress_port);
  }
}

// Modify the P4Info based on a regex match in the DebugString.
void RegexModifyP4Info(p4::config::v1::P4Info& p4info, absl::string_view regex,
                       absl::string_view replacement) {
  std::string p4info_str = p4info.DebugString();
  ASSERT_TRUE(RE2::Replace(&p4info_str, regex, replacement))
      << "Failed to perform P4Info replacement of regex " << regex;
  ASSERT_OK_AND_ASSIGN(p4info,
                       ParseTextProto<p4::config::v1::P4Info>(p4info_str));
}

// Retrieve the current known port IDs from the switch. Must use numerical port
// id names.
void GetPortIds(thinkit::Switch& target, std::vector<std::string>& interfaces,
                absl::btree_set<int>& port_ids) {
  ASSERT_OK_AND_ASSIGN(auto sut_gnmi_stub, target.CreateGnmiStub());
  ASSERT_OK_AND_ASSIGN(const auto interface_id_map,
                       GetAllInterfaceNameToPortId(*sut_gnmi_stub));
  ASSERT_OK_AND_ASSIGN(const auto up_interfaces,
                       GetUpInterfacesOverGnmi(*sut_gnmi_stub));

  for (const auto& interface_name : up_interfaces) {
    int port_id;
    ASSERT_THAT(interface_id_map, Contains(Key(interface_name)));
    ASSERT_TRUE(
        absl::SimpleAtoi(interface_id_map.at(interface_name), &port_id));
    port_ids.insert(port_id);
    interfaces.push_back(interface_name);
  }
}

}  // namespace

void HashConfigTest::TestData::AddPacket(absl::string_view egress_port,
                                         packetlib::Packet packet) {
  absl::StatusOr<int> status_or_index = GetPacketIndex(packet);
  if (status_or_index.ok()) {
    absl::MutexLock lock(&mutex_);
    packets_by_port_[egress_port].insert(*status_or_index);
    received_packets_.push_back({std::string(egress_port), absl::move(packet)});
  } else {
    // Ignore packets that don't match.
    VLOG(1) << "Received unexpected packet: " << packet.ShortDebugString()
            << ". " << status_or_index.status();
  }
}

absl::Status HashConfigTest::TestData::Log(
    thinkit::TestEnvironment& environment, absl::string_view artifact_name)
    ABSL_LOCKS_EXCLUDED(mutex_) {
  absl::MutexLock lock(&mutex_);
  std::string packet_log;
  for (const auto& [port, packet] : received_packets_) {
    absl::StrAppend(&packet_log, port, ": ", packet.ShortDebugString(), "\n");
  }
  return environment.StoreTestArtifact(absl::StrCat(artifact_name, ".txt"),
                                       packet_log);
}

void HashConfigTest::SetUp() {
  MirrorTestbedFixture::SetUp();

  ASSERT_NO_FATAL_FAILURE(
      InitializeTestbed(GetMirrorTestbed(), gnmi_config(), p4_info()));

  ASSERT_NO_FATAL_FAILURE(
      GetPortIds(GetMirrorTestbed().Sut(), interfaces_, port_ids_));
  LOG(INFO) << "Using ports: [" << absl::StrJoin(port_ids_, ", ") << "]";
  ASSERT_GE(port_ids_.size(), kMinimumMembersForTest);

  ASSERT_NO_FATAL_FAILURE(InitializeOriginalP4InfoTestDataIfNeeded());
}

void HashConfigTest::TearDown() {
  // Clean up flows on the control switch. We're using a non-fatal failure
  // here so we continue cleanup.
  auto result_or_control_p4_session =
      pdpi::P4RuntimeSession::Create(GetMirrorTestbed().ControlSwitch());
  if (result_or_control_p4_session.ok()) {
    EXPECT_OK(pdpi::ClearTableEntries(result_or_control_p4_session->get()))
        << "failed to clean up control switch P4 entries.";
  } else {
    ADD_FAILURE() << "failed to connect to control switch: "
                  << result_or_control_p4_session.status();
  }

  SCOPED_TRACE("Failed to restore base gNMI config.");
  EXPECT_OK(SaveSwitchLogs("teardown_before_reboot"));
  RebootSut();  // Ignore fatal failures to continue cleanup.
  MirrorTestbedFixture::TearDown();
}

absl::Status HashConfigTest::RecordP4Info(
    absl::string_view test_stage, const p4::config::v1::P4Info& p4info) {
  return GetMirrorTestbed().Environment().StoreTestArtifact(
      absl::StrCat(test_stage, "_p4info.pb.txt"), p4info.DebugString());
}

void HashConfigTest::RebootSut() {
  constexpr absl::Duration kRebootTimeout = absl::Minutes(7);
  absl::Time reboot_deadline = absl::Now() + kRebootTimeout;

  // Reboot the switch.
  thinkit::Switch& sut = GetMirrorTestbed().Sut();
  ASSERT_NO_FATAL_FAILURE(TestGnoiSystemColdReboot(sut));

  // Wait for port set-up to complete from coldboot config push recovery.
  ASSERT_OK(PushGnmiConfig(sut, gnmi_config()))
      << "Failed to push config after reboot.";

  ASSERT_OK(
      WaitForGnmiPortIdConvergence(GetMirrorTestbed().Sut(), gnmi_config(),
                                   /*timeout=*/reboot_deadline - absl::Now()));
  ASSERT_OK(WaitForCondition(PortsUp,
                             /*timeout=*/reboot_deadline - absl::Now(),
                             GetMirrorTestbed().Sut(), interfaces_,
                             /*with_healthz=*/false));

  // Wait for P4Runtime to be reachable.
  absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>> status_or_p4_session;
  do {
    status_or_p4_session = pdpi::P4RuntimeSession::Create(sut);
  } while (!status_or_p4_session.ok() && absl::Now() < reboot_deadline);
  ASSERT_OK(status_or_p4_session)
      << "Switch failed to reboot and come up after " << kRebootTimeout;
}

void HashConfigTest::SendAndReceivePackets(const pdpi::IrP4Info& ir_p4info,
                                           absl::string_view test_stage,
                                           absl::string_view test_config_name,
                                           const TestConfiguration& test_config,
                                           TestData& test_data) {
  SCOPED_TRACE(absl::StrCat("Failed while testing config: ", test_config_name));
  int ingress_port = *port_ids_.begin();

  // Set up the receive thread to record packets output by the SUT.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> control_p4_session,
      pdpi::P4RuntimeSession::Create(GetMirrorTestbed().ControlSwitch()));
  std::thread receive_packet_thread(
      absl::bind_front(ReceivePacketsUntilStreamIsClosed, &GetMirrorTestbed(),
                       &ir_p4info, control_p4_session.get(), &test_data));

  // Inject the packets.
  std::vector<packetlib::Packet> injected_packets;
  SendPackets(ir_p4info, test_config, *control_p4_session, ingress_port,
              injected_packets);
  LogPackets(
      GetMirrorTestbed().Environment(), injected_packets,
      absl::StrCat(test_stage, "_", test_config_name, "_injected_packets"));

  // Wait for all the packets to arrive.
  absl::Time deadline = absl::Now() + absl::Seconds(30);
  while (test_data.PacketCount() < kNumPackets && absl::Now() < deadline) {
    absl::SleepFor(absl::Seconds(1));
  }
  EXPECT_OK(test_data.Log(
      GetMirrorTestbed().Environment(),
      absl::StrCat(test_stage, "_", test_config_name, "_received_packets")));
  EXPECT_EQ(test_data.PacketCount(), kNumPackets)
      << "Unexpected number of packets received.";

  // Clean up.
  control_p4_session->TryCancel();
  receive_packet_thread.join();
}

void HashConfigTest::SendPacketsAndRecordResultsPerTestConfig(
    const p4::config::v1::P4Info p4info, absl::string_view test_stage,
    absl::node_hash_map<std::string, TestData>& output_record) {
  ASSERT_OK_AND_ASSIGN(pdpi::IrP4Info ir_p4info, pdpi::CreateIrP4Info(p4info));
  for (const auto& [config_name, test_config] : TestConfigs()) {
    ASSERT_NO_FATAL_FAILURE(SendAndReceivePackets(ir_p4info, test_stage,
                                                  config_name, test_config,
                                                  output_record[config_name]));
  }
}

void HashConfigTest::InitializeOriginalP4InfoTestDataIfNeeded() {
  if (original_p4info_test_data_ != nullptr) return;

  std::string test_stage = "0_original";
  EXPECT_OK(RecordP4Info(test_stage, p4_info()));
  ASSERT_NO_FATAL_FAILURE(
      ProgramHashingEntities(GetMirrorTestbed(), p4_info(), port_ids_));

  original_p4info_test_data_ = new absl::node_hash_map<std::string, TestData>();
  ASSERT_NO_FATAL_FAILURE(SendPacketsAndRecordResultsPerTestConfig(
      p4_info(), test_stage, *original_p4info_test_data_));
  ASSERT_NO_FATAL_FAILURE(RebootSut());
}

void HashConfigTest::TestHashDifference(
    const p4::config::v1::P4Info& modified_p4info, absl::string_view stage) {
  absl::node_hash_map<std::string, TestData> modified_hash_test_data;
  {
    SCOPED_TRACE("Failed to test modified p4info.");
    EXPECT_OK(RecordP4Info(stage, modified_p4info));
    ASSERT_NO_FATAL_FAILURE(
        ProgramHashingEntities(GetMirrorTestbed(), modified_p4info, port_ids_));
    ASSERT_NO_FATAL_FAILURE(SendPacketsAndRecordResultsPerTestConfig(
        modified_p4info, stage, modified_hash_test_data));
  }

  for (const auto& config : TestConfigNames()) {
    EXPECT_THAT(modified_hash_test_data.at(config).Results(),
                Not(UnorderedElementsAreArray(
                    OriginalP4InfoTestData().at(config).Results())))
        << "No hash diff found for config: " << config;
  }
}

void HashConfigTest::TestHashDifferenceWithBackup(
    const p4::config::v1::P4Info& modified_p4info,
    const p4::config::v1::P4Info& backup_p4info) {
  absl::node_hash_map<std::string, TestData> modified_hash_test_data;
  {
    SCOPED_TRACE("Failed to test modified p4info.");
    std::string stage = "1_modified";
    EXPECT_OK(RecordP4Info(stage, modified_p4info));
    ASSERT_NO_FATAL_FAILURE(
        ProgramHashingEntities(GetMirrorTestbed(), modified_p4info, port_ids_));
    ASSERT_NO_FATAL_FAILURE(SendPacketsAndRecordResultsPerTestConfig(
        modified_p4info, stage, modified_hash_test_data));
  }

  for (const auto& config : TestConfigNames()) {
    if (Matches(UnorderedElementsAreArray(
            OriginalP4InfoTestData().at(config).Results()))(
            modified_hash_test_data.at(config).Results())) {
      LOG(WARNING) << "No hash diff found for config: " << config
                   << ". Retesting with backup config.";
      // If any test fails, use the backup config.
      ASSERT_NO_FATAL_FAILURE(RebootSut());
      TestHashDifference(backup_p4info, "2_backup");
      return;
    }
  }
}

TEST_P(HashConfigTest, HashIsStableWithSameP4Info) {
  // Set up the switch with the original P4Info.
  std::string test_stage = "1_original";
  EXPECT_OK(RecordP4Info(test_stage, p4_info()));
  ASSERT_NO_FATAL_FAILURE(
      ProgramHashingEntities(GetMirrorTestbed(), p4_info(), port_ids_));

  // Send packets and record hash results.
  absl::node_hash_map<std::string, TestData> hash_test_data;
  ASSERT_NO_FATAL_FAILURE(SendPacketsAndRecordResultsPerTestConfig(
      p4_info(), test_stage, hash_test_data));

  // Ensure that the same packet set with the same hash parameters produces
  // the same result.
  for (const auto& config : TestConfigNames()) {
    EXPECT_THAT(hash_test_data.at(config).Results(),
                UnorderedElementsAreArray(
                    OriginalP4InfoTestData().at(config).Results()))
        << "No hash diff found for config: " << config;
  }
}

TEST_P(HashConfigTest, HashAlgorithmSettingsAffectPacketHash) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "1de932e8-666c-4ee4-960f-3a3aac717a25");

  p4::config::v1::P4Info modified_p4info = p4_info();
  ASSERT_NO_FATAL_FAILURE(
      RegexModifyP4Info(modified_p4info, R"re(sai_hash_algorithm\([^)]*\))re",
                        "sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"));
  // If we happen to match, attempt with another algorithm.
  if (Matches(EqualsProto(p4_info()))(modified_p4info)) {
    ASSERT_NO_FATAL_FAILURE(
        RegexModifyP4Info(modified_p4info, R"re(sai_hash_algorithm\([^)]*\))re",
                          "sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_CCITT)"));
  }
  ASSERT_THAT(modified_p4info, Not(EqualsProto(p4_info())))
      << "Failed to modify the hash algorithm in the P4Info.";

  ASSERT_NO_FATAL_FAILURE(TestHashDifference(modified_p4info));
}

TEST_P(HashConfigTest, HashOffsetSettingsAffectPacketHash) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "0a584c71-a701-4ea5-b4f3-5e4e37171d9c");

  p4::config::v1::P4Info modified_p4info = p4_info();
  ASSERT_NO_FATAL_FAILURE(RegexModifyP4Info(modified_p4info,
                                            R"re(sai_hash_offset\([^)]*\))re",
                                            "sai_hash_offset(3)"));
  // If we happen to match, attempt with another offset.
  if (Matches(EqualsProto(p4_info()))(modified_p4info)) {
    ASSERT_NO_FATAL_FAILURE(RegexModifyP4Info(modified_p4info,
                                              R"re(sai_hash_offset\([^)]*\))re",
                                              "sai_hash_offset(4)"));
  }
  ASSERT_THAT(modified_p4info, Not(EqualsProto(p4_info())))
      << "Failed to modify the hash offset in the P4Info.";

  ASSERT_NO_FATAL_FAILURE(TestHashDifference(modified_p4info));
}

// Tests that the hash seed impacts the hash result. Does not require that each
// hash seed produces a unique result but most seed differences should result
// in a hash difference. The test offers some leniency to prevent flakiness
// due to the lack of a uniqueness requirement.
TEST_P(HashConfigTest, HashSeedSettingsAffectPacketHash) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "13170845-0d6d-4ff6-aa1f-873c349ba84e");

  p4::config::v1::P4Info modified_p4info = p4_info();
  ASSERT_NO_FATAL_FAILURE(RegexModifyP4Info(modified_p4info,
                                            R"re(sai_hash_seed\([^)]*\))re",
                                            "sai_hash_seed(2821017091)"));
  // If we happen to match, attempt with another seed.
  if (Matches(EqualsProto(p4_info()))(modified_p4info)) {
    ASSERT_NO_FATAL_FAILURE(RegexModifyP4Info(modified_p4info,
                                              R"re(sai_hash_seed\([^)]*\))re",
                                              "sai_hash_seed(2821017092)"));
  }
  ASSERT_THAT(modified_p4info, Not(EqualsProto(p4_info())))
      << "Failed to modify the hash seed in the P4Info.";

  // Because we start with a random hash seed, there is some inherent
  // undeterminism in this test. We don't require that each hash seed results
  // in a unique hash. Instead, we allow for a backup test seed in case the
  // original seed doesn't produce a difference.
  p4::config::v1::P4Info backup_p4info = p4_info();
  ASSERT_NO_FATAL_FAILURE(RegexModifyP4Info(backup_p4info,
                                            R"re(sai_hash_seed\([^)]*\))re",
                                            "sai_hash_seed(1111111111)"));
  if (Matches(EqualsProto(p4_info()))(backup_p4info)) {
    ASSERT_NO_FATAL_FAILURE(RegexModifyP4Info(backup_p4info,
                                              R"re(sai_hash_seed\([^)]*\))re",
                                              "sai_hash_seed(1111111112)"));
  }
  ASSERT_THAT(backup_p4info, Not(EqualsProto(p4_info())))
      << "Failed to modify the hash seed in the P4Info.";

  ASSERT_NO_FATAL_FAILURE(
      TestHashDifferenceWithBackup(modified_p4info, backup_p4info));
}

}  // namespace pins_test

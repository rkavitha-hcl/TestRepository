// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tests/qos/cpu_qos_test.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "include/nlohmann/json.hpp"
#include "lib/gnmi/gnmi_helper.h"
#include "lib/ixia_helper.h"
#include "lib/p4rt/packet_listener.h"
#include "lib/validator/validator_lib.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "p4_pdpi/netaddr/mac_address.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/pd.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "thinkit/generic_testbed.h"
#include "thinkit/proto/generic_testbed.pb.h"
#include "thinkit/switch.h"

namespace pins_test {
namespace {

// Packet receiver thread to receive punted packets from switch over a P4
// session. The callback is invoked serially for every packet received.
// Example:
// PacketInReceiver receiver(
//      "SUT", p4_session,
//      [&num_packets_punted]() -> absl::Status {
//          num_packets_punted++;
//      });
//      .. do stuff
//      receiver.Destroy();
class PacketInReceiver final {
 public:
  PacketInReceiver(std::string name, pdpi::P4RuntimeSession *session,
                   std::function<void()> callback)
      : session_(session),
        receiver_([this, name, callback = std::move(callback)]() {
          p4::v1::StreamMessageResponse pi_response;
          // To break out of this loop invoke Destroy().
          while (session_->StreamChannelRead(pi_response)) {
            if (pi_response.has_packet()) {
              callback();
            }
          }
        }) {}

  PacketInReceiver() = delete;

  // It's ok to call this function multiple times.
  void Destroy() {
    session_->TryCancel();
    if (receiver_.joinable()) {
      receiver_.join();
    }
  }

  ~PacketInReceiver() { Destroy(); }

 private:
  pdpi::P4RuntimeSession *session_;
  std::thread receiver_;
};

struct QueueInfo {
  std::string gnmi_queue_name;      // Openconfig queue name.
  std::string p4_queue_name;        // P4 queue name.
  int rate_packets_per_second = 0;  // Rate of packets in packets per second.
};

absl::StatusOr<absl::flat_hash_map<std::string, QueueInfo>>
GetDefaultQueueInfo() {
  return absl::flat_hash_map<std::string, QueueInfo>{
      {"BE1", QueueInfo{"BE1", "0x2", 120}},
      {"AF1", QueueInfo{"AF1", "0x3", 120}},
      {"AF2", QueueInfo{"AF2", "0x4", 800}},
      {"AF3", QueueInfo{"AF3", "0x5", 120}},
      {"AF4", QueueInfo{"AF4", "0x6", 4000}},
      {"LLQ1", QueueInfo{"LLQ1", "0x0", 800}},
      {"LLQ2", QueueInfo{"LLQ2", "0x1", 800}},
      {"NC1", QueueInfo{"NC1", "0x7", 16000}},
  };
}

// Set up the switch to punt packets to CPU.
absl::Status SetUpPuntToCPU(const netaddr::MacAddress &dmac,
                            const netaddr::Ipv4Address &src_ip,
                            const netaddr::Ipv4Address &dst_ip,
                            absl::string_view p4_queue,
                            const p4::config::v1::P4Info &p4info,
                            pdpi::P4RuntimeSession &p4_session) {
  ASSIGN_OR_RETURN(auto ir_p4info, pdpi::CreateIrP4Info(p4info));

  RETURN_IF_ERROR(pdpi::SetForwardingPipelineConfig(
      &p4_session,
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT, p4info))
      << "SetForwardingPipelineConfig: Failed to push P4Info: ";

  RETURN_IF_ERROR(pdpi::ClearTableEntries(&p4_session));

  auto acl_entry = gutil::ParseProtoOrDie<sai::TableEntry>(absl::Substitute(
      R"pb(
        acl_ingress_table_entry {
          match {
            dst_mac { value: "$0" mask: "ff:ff:ff:ff:ff:ff" }
            is_ipv4 { value: "0x1" }
            src_ip { value: "$1" mask: "255.255.255.255" }
            dst_ip { value: "$2" mask: "255.255.255.255" }
          }
          action { trap { qos_queue: "$3" } }
          priority: 1
        }
      )pb",
      dmac.ToString(), src_ip.ToString(), dst_ip.ToString(), p4_queue));
  std::vector<p4::v1::TableEntry> pi_entries;
  ASSIGN_OR_RETURN(
      pi_entries.emplace_back(), pdpi::PdTableEntryToPi(ir_p4info, acl_entry),
      _.SetPrepend() << "Failed in PD table conversion to PI, entry: "
                     << acl_entry.DebugString() << " error: ");

  LOG(INFO) << "InstallPiTableEntries";
  return pdpi::InstallPiTableEntries(&p4_session, ir_p4info, pi_entries);
}

// These are the counters we track in these tests.
struct QueueCounters {
  int64_t num_packets_transmitted = 0;
  int64_t num_packet_dropped = 0;
};

// TODO: Move this to a helper library.
absl::StatusOr<QueueCounters> GetGnmiQueueStat(
    absl::string_view port, absl::string_view queue,
    gnmi::gNMI::StubInterface &gnmi_stub) {
  QueueCounters counters;

  const std::string openconfig_transmit_count_state_path = absl::Substitute(
      "qos/interfaces/interface[interface-id=$0]"
      "/output/queues/queue[name=$1]/state/transmit-pkts",
      port, queue);

  ASSIGN_OR_RETURN(
      std::string transmit_counter_response,
      GetGnmiStatePathInfo(&gnmi_stub, openconfig_transmit_count_state_path,
                           "openconfig-qos:transmit-pkts"));

  if (!absl::SimpleAtoi(StripQuotes(transmit_counter_response),
                        &counters.num_packets_transmitted)) {
    return absl::InternalError(absl::StrCat("Unable to parse counter from ",
                                            transmit_counter_response));
  }

  const std::string openconfig_drop_count_state_path = absl::Substitute(
      "qos/interfaces/interface[interface-id=$0]"
      "/output/queues/queue[name=$1]/state/dropped-pkts",
      port, queue);

  ASSIGN_OR_RETURN(
      std::string drop_counter_response,
      GetGnmiStatePathInfo(&gnmi_stub, openconfig_drop_count_state_path,
                           "openconfig-qos:dropped-pkts"));

  if (!absl::SimpleAtoi(StripQuotes(drop_counter_response),
                        &counters.num_packet_dropped)) {
    return absl::InternalError(
        absl::StrCat("Unable to parse counter from ", drop_counter_response));
  }
  return counters;
}

absl::Status SetPortSpeed(const std::string &port_speed,
                          const std::string &iface,
                          gnmi::gNMI::StubInterface &gnmi_stub) {
  std::string ops_config_path = absl::StrCat(
      "interfaces/interface[name=", iface, "]/ethernet/config/port-speed");
  std::string ops_val =
      absl::StrCat("{\"openconfig-if-ethernet:port-speed\":", port_speed, "}");

  RETURN_IF_ERROR(pins_test::SetGnmiConfigPath(&gnmi_stub, ops_config_path,
                                               GnmiSetType::kUpdate, ops_val));

  return absl::OkStatus();
}

absl::StatusOr<bool> CheckLinkUp(const std::string &iface,
                                 gnmi::gNMI::StubInterface &gnmi_stub) {
  std::string oper_status_state_path =
      absl::StrCat("interfaces/interface[name=", iface, "]/state/oper-status");

  std::string parse_str = "openconfig-interfaces:oper-status";
  ASSIGN_OR_RETURN(
      std::string ops_response,
      GetGnmiStatePathInfo(&gnmi_stub, oper_status_state_path, parse_str));

  return ops_response == "\"UP\"";
}

TEST_P(CpuQosIxiaTestFixture, TestCPUQueueRateLimit) {
  // Pick a testbed with an Ixia Traffic Generator.
  auto requirements =
      gutil::ParseProtoOrDie<thinkit::TestRequirements>(
          R"pb(interface_requirements {
                 count: 1
                 interface_mode: TRAFFIC_GENERATOR
               })pb");

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<thinkit::GenericTestbed> generic_testbed,
      GetParam().testbed_interface->GetTestbedWithRequirements(requirements));

  // Set test case ID.
  generic_testbed->Environment().SetTestCaseID(
      "15830795-b6db-415e-835b-beae6aa59204");

  ASSERT_OK(generic_testbed->Environment().StoreTestArtifact(
      "gnmi_config.txt", GetParam().gnmi_config));

  thinkit::Switch &sut = generic_testbed->Sut();

  // Push GNMI config.
  ASSERT_OK(pins_test::PushGnmiConfig(sut, GetParam().gnmi_config));

  // Hook up to GNMI.
  ASSERT_OK_AND_ASSIGN(auto gnmi_stub, sut.CreateGnmiStub());

  // Get Queues
  // TODO: Extract Queue info from config instead of hardcoded
  // default.
  ASSERT_OK_AND_ASSIGN(auto queues, GetDefaultQueueInfo());

  // Set up P4Runtime session.
  // TODO: Use `CreateWithP4InfoAndClearTables` cl/397193959 when
  // its available.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> sut_p4_session,
                       pdpi::P4RuntimeSession::Create(generic_testbed->Sut()));
  auto clear_table_entries = absl::Cleanup(
      [&]() { ASSERT_OK(pdpi::ClearTableEntries(sut_p4_session.get())); });

  // Flow details.
  const auto dest_mac = netaddr::MacAddress(02, 02, 02, 02, 02, 02);
  const auto source_mac = netaddr::MacAddress(00, 01, 02, 03, 04, 05);
  const auto source_ip = netaddr::Ipv4Address(192, 168, 10, 1);
  const auto dest_ip = netaddr::Ipv4Address(172, 0, 0, 1);

  // BE1 is guaranteed to exist in the map which is currently hardocoded
  // and we will test for BE1 queue.
  // TODO: When we replace hardcoding with extraction of members
  // from the config, we need to add iteration logic to go over the configured
  // queues.
  QueueInfo queue_under_test = queues["BE1"];

  ASSERT_OK(SetUpPuntToCPU(dest_mac, source_ip, dest_ip,
                           queue_under_test.p4_queue_name, GetParam().p4info,
                           *sut_p4_session));

  // Listen for punted packets from the SUT.
  int num_packets_punted = 0;
  absl::Time time_first_packet_punted, time_last_packet_punted;

  PacketInReceiver receiver("SUT", sut_p4_session.get(),
                            [&num_packets_punted, &time_first_packet_punted,
                             &time_last_packet_punted]() {
                              if (num_packets_punted == 0) {
                                time_first_packet_punted = absl::Now();
                              }
                              time_last_packet_punted = absl::Now();
                              num_packets_punted++;
                              return;
                            });

  // Go through all the ports that interface to the Ixia and set them
  // to 100GB since the Ixia ports are all 100GB.
  absl::flat_hash_map<std::string, thinkit::InterfaceInfo> interface_info =
      generic_testbed->GetSutInterfaceInfo();
  for (const auto &[interface, info] : interface_info) {
    if (info.interface_mode == thinkit::TRAFFIC_GENERATOR) {
      ASSERT_OK(SetPortSpeed("\"openconfig-if-ethernet:SPEED_100GB\"",
                             interface, *gnmi_stub));
    }
  }

  // Wait to let the links come up. Switch guarantees state paths to reflect
  // in 10s. Lets wait for a bit more.
  absl::SleepFor(absl::Seconds(15));

  // TODO: Move this to helper function.
  // Loop through the interface_info looking for Ixia/SUT interface pairs,
  // checking if the link is up.  we need one pair with link up for the
  // ingress interface/IXIA traffic generation.
  std::string ixia_interface;
  std::string sut_interface;
  bool sut_link_up = false;
  for (const auto &[interface, info] : interface_info) {
    if (info.interface_mode == thinkit::TRAFFIC_GENERATOR) {
      ASSERT_OK_AND_ASSIGN(sut_link_up, CheckLinkUp(interface, *gnmi_stub));
      if (sut_link_up) {
        ixia_interface = info.peer_interface_name;
        sut_interface = interface;
        break;
      }
    }
  }

  ASSERT_TRUE(sut_link_up);

  constexpr float kTolerancePercent = 2.0;
  constexpr int kFramesPerSecond = 1000000;
  constexpr int kTotalFrames = 10000000;
  const absl::Duration kTrafficDuration =
      absl::Seconds(kTotalFrames / kFramesPerSecond);
  constexpr int kFrameSize = 1514;

  // Set up Ixia traffic.
  // Send Ixia traffic.
  // Stop Ixia traffic.

  ASSERT_OK_AND_ASSIGN(ixia::IxiaPortInfo ixia_port,
                       ixia::ExtractPortInfo(ixia_interface));

  ASSERT_OK_AND_ASSIGN(
      std::string topology_ref,
      pins_test::ixia::IxiaConnect(ixia_port.hostname, *generic_testbed));

  ASSERT_OK_AND_ASSIGN(
      std::string vport_ref,
      pins_test::ixia::IxiaVport(topology_ref, ixia_port.card, ixia_port.port,
                                 *generic_testbed));

  ASSERT_OK_AND_ASSIGN(
      std::string traffic_ref,
      pins_test::ixia::IxiaSession(vport_ref, *generic_testbed));

  ASSERT_OK(pins_test::ixia::SetFrameRate(traffic_ref, kFramesPerSecond,
                                          *generic_testbed));

  ASSERT_OK(pins_test::ixia::SetFrameCount(traffic_ref, kTotalFrames,
                                           *generic_testbed));

  ASSERT_OK(
      pins_test::ixia::SetFrameSize(traffic_ref, kFrameSize, *generic_testbed));

  ASSERT_OK(pins_test::ixia::SetSrcMac(traffic_ref, source_mac.ToString(),
                                       *generic_testbed));

  ASSERT_OK(pins_test::ixia::SetDestMac(traffic_ref, dest_mac.ToString(),
                                        *generic_testbed));

  ASSERT_OK(pins_test::ixia::AppendIPv4(traffic_ref, *generic_testbed));

  ASSERT_OK(pins_test::ixia::SetSrcIPv4(traffic_ref, source_ip.ToString(),
                                        *generic_testbed));

  ASSERT_OK(pins_test::ixia::SetDestIPv4(traffic_ref, dest_ip.ToString(),
                                         *generic_testbed));

  ASSERT_OK_AND_ASSIGN(
      QueueCounters initial_counters,
      GetGnmiQueueStat("CPU", queue_under_test.gnmi_queue_name, *gnmi_stub));

  ASSERT_OK(pins_test::ixia::StartTraffic(traffic_ref, topology_ref,
                                          *generic_testbed));

  // Wait for Traffic to be sent.
  absl::SleepFor(kTrafficDuration);

  ASSERT_OK(pins_test::ixia::StopTraffic(traffic_ref, *generic_testbed));

  static constexpr absl::Duration kPollInterval = absl::Seconds(5);
  static constexpr absl::Duration kTotalTime = absl::Seconds(30);
  static const int kIterations = kTotalTime / kPollInterval;

  QueueCounters final_counters;
  QueueCounters delta_counters;
  // Check for counters every 5 seconds upto 30 seconds till they match.
  for (int gnmi_counters_check = 0; gnmi_counters_check < kIterations;
       gnmi_counters_check++) {
    absl::SleepFor(kPollInterval);
    ASSERT_OK_AND_ASSIGN(
        final_counters,
        GetGnmiQueueStat("CPU", queue_under_test.gnmi_queue_name, *gnmi_stub));
    delta_counters = {
        .num_packets_transmitted = final_counters.num_packets_transmitted -
                                   initial_counters.num_packets_transmitted,
        .num_packet_dropped = final_counters.num_packet_dropped -
                              initial_counters.num_packet_dropped,
    };
    LOG(INFO) << "Tx = " << delta_counters.num_packets_transmitted
              << " Drop = " << delta_counters.num_packet_dropped;
    if (delta_counters.num_packets_transmitted +
            delta_counters.num_packet_dropped ==
        kTotalFrames) {
      break;
    }
    ASSERT_NE(gnmi_counters_check, kIterations - 1)
        << "GNMI packet count "
        << delta_counters.num_packets_transmitted +
               delta_counters.num_packet_dropped
        << " != Packets sent from Ixia " << kTotalFrames;
  }

  // Stop receiving at tester.
  receiver.Destroy();

  // Verify the received packets matches gNMI queue stats.
  ASSERT_EQ(num_packets_punted, delta_counters.num_packets_transmitted);

  {
    absl::Duration duration =
        time_last_packet_punted - time_first_packet_punted;

    LOG(INFO) << "Packets received at Controller: " << num_packets_punted;
    LOG(INFO) << "Timestamp of first received packet: "
              << time_first_packet_punted;
    LOG(INFO) << "Timestamp of last received packet: "
              << time_last_packet_punted;
    LOG(INFO) << "Duration of packets received: " << duration;
    int rate_received = 0;
    if (int64_t useconds = absl::ToInt64Microseconds(duration); useconds != 0) {
      rate_received = num_packets_punted * 1000000 / useconds;
      LOG(INFO) << "Rate of packets received (pps): " << rate_received;
    }
    EXPECT_LT(rate_received, queue_under_test.rate_packets_per_second *
                                 (1 + kTolerancePercent / 100));
    EXPECT_GT(rate_received, queue_under_test.rate_packets_per_second *
                                 (1 - kTolerancePercent / 100));
  }
}

}  // namespace
}  // namespace pins_test

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
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/gnmi/gnmi_helper.h"
#include "lib/p4rt/packet_listener.h"
#include "lib/validator/validator_lib.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/pd.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "single_include/nlohmann/json.hpp"
#include "thinkit/generic_testbed.h"
#include "thinkit/proto/generic_testbed.pb.h"
#include "thinkit/switch.h"

namespace pins_test {

class PacketInReceiver final {
 public:
  PacketInReceiver(std::string name, pdpi::P4RuntimeSession* session,
                   std::function<absl::Status()> callback)
      : session_(session), receiver_([this, name, callback]() {
          p4::v1::StreamMessageResponse pi_response;
          // The only way to break out of this loop is for the stream channel to
          // be closed. gRPC does not support selecting on both stream Read and
          // fiber Cancel.
          while (session_->StreamChannelRead(pi_response)) {
            if (pi_response.has_packet()) {
              ASSERT_OK(callback())
                  << " packet in handling failed for " << name;
            }
          }
        }) {}

  PacketInReceiver() = delete;

  // It's ok to call this function multiple times.
  void Destroy() {
    session_->TryCancel();  // Needed so fiber stops looping.
    receiver_.join();
  }

  ~PacketInReceiver() { Destroy(); }

 private:
  pdpi::P4RuntimeSession* session_;
  std::thread receiver_;
};

TEST_P(CpuQosIxiaTestFixture, TestCPUQueueRateLimit) {
  // Pick a testbed with an Ixia Traffic Generator. A SUT is assumed.
  auto requirements =
      gutil::ParseProtoOrDie<thinkit::TestRequirements>(
          R"pb(interface_requirements {
                 count: 1
                 interface_mode: TRAFFIC_GENERATOR
               })pb");

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<thinkit::GenericTestbed> generic_testbed,
      GetParam().testbed_interface->GetTestbedWithRequirements(requirements));

  thinkit::Switch& sut = generic_testbed->Sut();

  // Connect to TestTracker for test status.
  if (auto& id = GetParam().test_case_id; id.has_value()) {
    generic_testbed->Environment().SetTestCaseID(*id);
  }

  // Push GNMI config.
  ASSERT_OK(pins_test::PushGnmiConfig(sut, GetParam().gnmi_config));

  // Hook up to GNMI.
  ASSERT_OK_AND_ASSIGN(auto gnmi_stub, sut.CreateGnmiStub());

  // Set up P4Runtime session..
  // TODO: Use `CreateWithP4InfoAndClearTables` cl/397193959 when
  // its available.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> sut_p4_session,
                       pdpi::P4RuntimeSession::Create(generic_testbed->Sut()));
  auto clear_table_entries = absl::Cleanup(
      [&]() { ASSERT_OK(pdpi::ClearTableEntries(sut_p4_session.get())); });

  // TODO: Set Up single P4RT punt flow.

  // Listen for punted packets from the SUT.
  int num_packets_punted = 0;
  absl::Time time_first_packet_punted, time_last_packet_punted;
  std::mutex counters_mutex;
  PacketInReceiver sut_fiber(
      "SUT", sut_p4_session.get(),
      [&num_packets_punted, &time_first_packet_punted, &time_last_packet_punted,
       &counters_mutex]() -> absl::Status {
        const std::lock_guard<std::mutex> lock(counters_mutex);
        if (num_packets_punted == 0) {
          time_first_packet_punted = absl::Now();
        }
        time_last_packet_punted = absl::Now();
        num_packets_punted++;
        return absl::OkStatus();
      });

  // TODO:
  // Setup Ixia traffic.
  // Send Ixia traffic.
  // Stop Ixia traffic.

  sut_fiber.Destroy();

  {
    const std::lock_guard<std::mutex> lock(counters_mutex);
    absl::Duration duration =
        time_last_packet_punted - time_first_packet_punted;
    LOG(INFO) << "Packets received at Controller: " << num_packets_punted;
    LOG(INFO) << "Timestamp of first received packet: "
              << time_first_packet_punted;
    LOG(INFO) << "Timestamp of last received packet: "
              << time_last_packet_punted;
    LOG(INFO) << "Duration of packets received: " << duration;
    if (int64_t seconds = absl::ToInt64Seconds(duration); seconds != 0) {
      LOG(INFO) << "Rate of packets received (pps): "
                << num_packets_punted / seconds;
    }
  }
}

}  // namespace pins_test

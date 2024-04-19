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

#include "tests/integration/system/random_blackbox_events_tests.h"

#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <thread>  //NOLINT
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "google/rpc/code.pb.h"
#include "grpcpp/client_context.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/basic_traffic/basic_traffic.h"
#include "lib/gnmi/gnmi_helper.h"
#include "lib/utils/generic_testbed_utils.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_fuzzer/annotation_util.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer.pb.h"
#include "p4_fuzzer/fuzzer_config.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "sai_p4/fixed/roles.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "thinkit/generic_testbed.h"
#include "thinkit/proto/generic_testbed.pb.h"

namespace pins_test {
namespace {

class ScopedThread {
 public:
  ScopedThread(std::function<void(const bool&)> body)
      : time_to_exit_(false),
        thread_(std::move(body), std::cref(time_to_exit_)) {}

  ~ScopedThread() {
    time_to_exit_ = true;
    thread_.join();
  }

 private:
  bool time_to_exit_;
  std::thread thread_;
};

}  // namespace

TEST_P(RandomBlackboxEventsTest, ControlPlaneWithTrafficWithoutValidation) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<thinkit::GenericTestbed> testbed,
                       GetTestbedWithRequirements(
                           gutil::ParseProtoOrDie<thinkit::TestRequirements>(
                               R"pb(interface_requirements {
                                      count: 2
                                      interface_mode: CONTROL_INTERFACE
                                    })pb")));
  ASSERT_OK_AND_ASSIGN(
      auto p4rt_session,
      pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
          testbed->Sut(), sai::GetP4Info(sai::Instantiation::kMiddleblock)));

  std::vector<std::string> sut_control_interfaces =
      GetSutInterfaces(FromTestbed(GetAllControlLinks, *testbed));

  ASSERT_OK_AND_ASSIGN(auto gnmi_stub, testbed->Sut().CreateGnmiStub());
  ASSERT_OK_AND_ASSIGN(auto port_id_map,
                       GetAllInterfaceNameToPortId(*gnmi_stub));
  std::vector<std::string> port_ids;
  port_ids.reserve(port_id_map.size());
  absl::c_transform(port_id_map, std::back_inserter(port_ids),
                    [](const auto& pair) { return pair.second; });

  p4_fuzzer::FuzzerConfig config = {
      .info = sai::GetIrP4Info(sai::Instantiation::kMiddleblock),
      .ports = std::move(port_ids),
      .qos_queues = {"0x0", "0x1", "0x2", "0x3", "0x4", "0x5", "0x6", "0x7"},
      .tables_for_which_to_not_exceed_resource_guarantees =
          {"vrf_table", "mirror_session_table"},
      .role = "sdn_controller",
      .mutate_update_probability = 0.1,
  };

  {
    ScopedThread p4rt_fuzzer([&config,
                              &p4rt_session](const bool& time_to_exit) {
      absl::BitGen generator;
      p4_fuzzer::SwitchState state(config.info);
      while (!time_to_exit) {
        p4_fuzzer::AnnotatedWriteRequest annotated_request =
            p4_fuzzer::FuzzWriteRequest(&generator, config, state,
                                        std::numeric_limits<int>::max());
        p4::v1::WriteRequest request =
            p4_fuzzer::RemoveAnnotations(annotated_request);
        request.set_device_id(p4rt_session->DeviceId());
        request.set_role(P4RUNTIME_ROLE_SDN_CONTROLLER);
        *request.mutable_election_id() = p4rt_session->ElectionId();

        grpc::ClientContext context;
        p4::v1::WriteResponse pi_response;
        ASSERT_OK_AND_ASSIGN(
            pdpi::IrWriteRpcStatus response,
            pdpi::GrpcStatusToIrWriteRpcStatus(
                p4rt_session->Stub().Write(&context, request, &pi_response),
                request.updates_size()));

        for (int i = 0; i < response.rpc_response().statuses().size(); i++) {
          const pdpi::IrUpdateStatus& status =
              response.rpc_response().statuses(i);
          const p4::v1::Update& update = request.updates(i);
          EXPECT_NE(status.code(), google::rpc::Code::INTERNAL)
              << "Fuzzing should never cause an INTERNAL error, but got: "
              << status.DebugString();

          if (status.code() == google::rpc::Code::OK) {
            ASSERT_OK(state.ApplyUpdate(update));
          }
        }

        EXPECT_OK(pdpi::ReadPiTableEntries(p4rt_session.get()).status());
      }
    });
    const auto test_packet = gutil::ParseProtoOrDie<packetlib::Packet>(R"pb(
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
        }
      }
      headers {
        udp_header { source_port: "0x0000" destination_port: "0x0000" }
      })pb");
    ASSERT_OK_AND_ASSIGN(
        auto statistics,
        basic_traffic::SendTraffic(
            *testbed, p4rt_session.get(),
            {basic_traffic::InterfacePair{
                .ingress_interface = sut_control_interfaces[0],
                .egress_interface = sut_control_interfaces[1]}},
            {test_packet}, absl::Minutes(5)));
    for (const basic_traffic::TrafficStatistic& statistic : statistics) {
      if (statistic.packets_sent != statistic.packets_received) {
        LOG(WARNING) << statistic.interfaces.ingress_interface << " -> "
                     << statistic.interfaces.egress_interface
                     << ": Count mismatch; sent " << statistic.packets_sent
                     << " received " << statistic.packets_received << ", "
                     << statistic.packets_routed_incorrectly
                     << " routed incorrectly.";
      }
    }
  }
}

}  // namespace pins_test

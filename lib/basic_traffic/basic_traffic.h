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

#ifndef GOOGLE_LIB_BASIC_TRAFFIC_BASIC_TRAFFIC_H_
#define GOOGLE_LIB_BASIC_TRAFFIC_BASIC_TRAFFIC_H_

#include <string>
#include <tuple>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "lib/basic_traffic/basic_p4rt_util.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "thinkit/generic_testbed.h"
#include "thinkit/switch.h"

namespace pins_test::basic_traffic {

// A struct that represents which SUT interface the packets ingress into and
// which SUT interface the packets egress out from.
struct InterfacePair {
  std::string ingress_interface;
  std::string egress_interface;

  // For unit testing purposes.
  bool operator==(const InterfacePair& other) const {
    return std::tie(ingress_interface, egress_interface) ==
           std::tie(other.ingress_interface, other.egress_interface);
  }
};

// A struct that represents the statistics for a specific flow (interfaces and
// packet pair).
struct TrafficStatistic {
  // The SUT ingress and egress interfaces.
  InterfacePair interfaces;

  // Number of packets sent.
  int packets_sent;

  // Number of packets received on the correct port.
  int packets_received;

  // Number of packets received but on a different port from what was expected.
  int packets_routed_incorrectly;

  // Contains the actual packet proto sent to the switch.
  packetlib::Packet packet;
};

// Options for the `SendTraffic` function.
struct SendTrafficOptions {
  // The approximate `packets_per_second` rate to send traffic at.
  int packets_per_second = 100;

  // The instantiation to be used to get a `P4Info`.
  sai::Instantiation instantiation = sai::Instantiation::kMiddleblock;

  // The function that handles a P4RT write request.
  WriteRequestHandler write_request = pdpi::SetMetadataAndSendPiWriteRequest;
};

// Returns a list of interface pairs generated by assigning one source to one
// destination in order.
// e.g. sources = (a, b), destinations = (c, d) -> pairs = ((a, c), (b, d)).
std::vector<InterfacePair> OneToOne(absl::Span<const std::string> sources,
                                    absl::Span<const std::string> destinations);

// Returns a list of interface pairs generated by assigning every source to
// every destination.
// e.g. sources = (a), destinations = (c, d) -> pairs = ((a, c), (a, d)).
std::vector<InterfacePair> ManyToMany(
    absl::Span<const std::string> sources,
    absl::Span<const std::string> destinations);

// Returns a list of interface pairs generated by assigninng every interface to
// every other interface.
// e.g. interfaces = (a, b, c) ->
//      pairs = ((a, b), (a, c), (b, a), (b, c), (c, a), (c, b)).
std::vector<InterfacePair> AllToAll(absl::Span<const std::string> interfaces);

// Sends traffic using the `GenericTestbed` by sending every packet through
// every interface pair continuously for a given `duration`.
absl::StatusOr<std::vector<TrafficStatistic>> SendTraffic(
    thinkit::GenericTestbed& testbed, pdpi::P4RuntimeSession* session,
    absl::Span<const InterfacePair> pairs,
    absl::Span<const packetlib::Packet> packets, absl::Duration duration,
    SendTrafficOptions options = SendTrafficOptions());

}  // namespace pins_test::basic_traffic

#endif  // GOOGLE_LIB_BASIC_TRAFFIC_BASIC_TRAFFIC_H_

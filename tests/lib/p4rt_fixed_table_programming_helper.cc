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
#include "tests/lib/p4rt_fixed_table_programming_helper.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "gutil/testing.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"

namespace gpins {

absl::StatusOr<p4::v1::Update> RouterInterfaceTableUpdate(
    const pdpi::IrP4Info& ir_p4_info, p4::v1::Update::Type type,
    absl::string_view router_interface_id, absl::string_view port,
    absl::string_view src_mac) {
  pdpi::IrUpdate ir_update;
  RETURN_IF_ERROR(gutil::ReadProtoFromString(
      absl::Substitute(R"pb(
                         type: $0
                         table_entry {
                           table_name: "router_interface_table"
                           matches {
                             name: "router_interface_id"
                             exact { str: "$1" }
                           }
                           action {
                             name: "set_port_and_src_mac"
                             params {
                               name: "port"
                               value { str: "$2" }
                             }
                             params {
                               name: "src_mac"
                               value { mac: "$3" }
                             }
                           }
                         }
                       )pb",
                       type, router_interface_id, port, src_mac),
      &ir_update))
      << "invalid pdpi::IrUpdate string.";
  return pdpi::IrUpdateToPi(ir_p4_info, ir_update);
}

absl::StatusOr<p4::v1::Update> NeighborTableUpdate(
    const pdpi::IrP4Info& ir_p4_info, p4::v1::Update::Type type,
    absl::string_view router_interface_id, absl::string_view neighbor_id,
    absl::string_view dst_mac) {
  pdpi::IrUpdate ir_update;
  RETURN_IF_ERROR(gutil::ReadProtoFromString(
      absl::Substitute(R"pb(
                         type: $0
                         table_entry {
                           table_name: "neighbor_table"
                           matches {
                             name: "router_interface_id"
                             exact { str: "$1" }
                           }
                           matches {
                             name: "neighbor_id"
                             exact { str: "$2" }
                           }
                           action {
                             name: "set_dst_mac"
                             params {
                               name: "dst_mac"
                               value { mac: "$3" }
                             }
                           }
                         }
                       )pb",
                       type, router_interface_id, neighbor_id, dst_mac),
      &ir_update))
      << "invalid pdpi::IrUpdate string.";
  return pdpi::IrUpdateToPi(ir_p4_info, ir_update);
}

absl::StatusOr<p4::v1::Update> NexthopTableUpdate(
    const pdpi::IrP4Info& ir_p4_info, p4::v1::Update::Type type,
    absl::string_view nexthop_id, absl::string_view router_interface_id,
    absl::string_view neighbor_id) {
  pdpi::IrUpdate ir_update;
  RETURN_IF_ERROR(gutil::ReadProtoFromString(
      absl::Substitute(R"pb(
                         type: $0
                         table_entry {
                           table_name: "nexthop_table"
                           matches {
                             name: "nexthop_id"
                             exact { str: "$1" }
                           }
                           action {
                             name: "set_nexthop"
                             params {
                               name: "router_interface_id"
                               value { str: "$2" }
                             }
                             params {
                               name: "neighbor_id"
                               value { str: "$3" }
                             }
                           }
                         }
                       )pb",
                       type, nexthop_id, router_interface_id, neighbor_id),
      &ir_update))
      << "invalid pdpi::IrUpdate string.";
  return pdpi::IrUpdateToPi(ir_p4_info, ir_update);
}

absl::StatusOr<p4::v1::Update> VrfTableUpdate(const pdpi::IrP4Info& ir_p4_info,
                                              p4::v1::Update::Type type,
                                              absl::string_view vrf_id) {
  if (vrf_id.empty()) {
    return absl::InvalidArgumentError(
        "Empty vrf id is reserved for default vrf.");
  }
  pdpi::IrUpdate ir_update;
  RETURN_IF_ERROR(gutil::ReadProtoFromString(
      absl::Substitute(R"pb(
                         type: $0
                         table_entry {
                           table_name: "vrf_table"
                           matches {
                             name: "vrf_id"
                             exact { str: "$1" }
                           }
                           action { name: "no_action" }
                         }
                       )pb",
                       type, vrf_id),
      &ir_update))
      << "invalid pdpi::IrUpdate string.";
  return pdpi::IrUpdateToPi(ir_p4_info, ir_update);
}

absl::StatusOr<p4::v1::Update> Ipv4TableUpdate(
    const pdpi::IrP4Info& ir_p4_info, p4::v1::Update::Type type,
    const IpTableOptions& ip_options) {
  pdpi::IrUpdate ir_update;
  ir_update.set_type(type);
  ir_update.mutable_table_entry()->set_table_name("ipv4_table");

  // Always set the VRF ID.
  auto* vrf_id = ir_update.mutable_table_entry()->add_matches();
  vrf_id->set_name("vrf_id");
  vrf_id->mutable_exact()->set_str(ip_options.vrf_id);

  // optionally set the IPv4 DST address.
  if (ip_options.dst_addr_lpm.has_value()) {
    auto* dst_addr = ir_update.mutable_table_entry()->add_matches();
    dst_addr->set_name("ipv4_dst");
    dst_addr->mutable_lpm()->mutable_value()->set_ipv4(
        ip_options.dst_addr_lpm->first);
    dst_addr->mutable_lpm()->set_prefix_length(ip_options.dst_addr_lpm->second);
  }

  std::string action_name;
  switch (ip_options.action) {
    case IpTableOptions::Action::kSetNextHopId:
      action_name = "set_nexthop_id";
      break;
    case IpTableOptions::Action::kDrop:
      action_name = "drop";
  }
  auto* action = ir_update.mutable_table_entry()->mutable_action();
  action->set_name(action_name);

  // optionally set the nexthop ID paramter
  if (ip_options.nexthop_id.has_value()) {
    auto* param = action->add_params();
    param->set_name("nexthop_id");
    param->mutable_value()->set_str(*ip_options.nexthop_id);
  }

  return pdpi::IrUpdateToPi(ir_p4_info, ir_update);
}

absl::StatusOr<p4::v1::Update> L3AdmitTableUpdate(
    const pdpi::IrP4Info& ir_p4_info, p4::v1::Update::Type type,
    const L3AdmitOptions& options) {
  pdpi::IrUpdate ir_update;
  ir_update.set_type(type);
  ir_update.mutable_table_entry()->set_table_name("l3_admit_table");

  // Always set the priority because the DST mac is a ternary value.
  ir_update.mutable_table_entry()->set_priority(options.priority);

  // Always set the DST mac.
  auto* dst_mac = ir_update.mutable_table_entry()->add_matches();
  dst_mac->set_name("dst_mac");
  dst_mac->mutable_ternary()->mutable_value()->set_mac(options.dst_mac.first);
  dst_mac->mutable_ternary()->mutable_mask()->set_mac(options.dst_mac.second);

  // Only set the port if it is passed.
  if (options.in_port.has_value()) {
    auto* in_port = ir_update.mutable_table_entry()->add_matches();
    in_port->set_name("in_port");
    in_port->mutable_optional()->mutable_value()->set_str(*options.in_port);
  }

  // Always set the action to "admit_to_l3"
  ir_update.mutable_table_entry()->mutable_action()->set_name("admit_to_l3");

  return pdpi::IrUpdateToPi(ir_p4_info, ir_update);
}

}  // namespace gpins

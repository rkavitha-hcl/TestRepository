// Copyright 2020 Google LLC
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
#include "p4rt_app/sonic/packetio_impl.h"

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/substitute.h"
#include "gutil/collections.h"

namespace p4rt_app {
namespace sonic {
namespace {

// Helper function to set socket map.
absl::flat_hash_map<std::string, int> CreateSocketMap(
    const std::vector<std::unique_ptr<sonic::PacketIoPortSockets>>&
        port_sockets) {
  absl::flat_hash_map<std::string, int> socket_map;
  // Populate the socket map.
  for (const auto& port : port_sockets) {
    socket_map[port->port_name] = port->port_socket;
  }
  return socket_map;
}

}  // namespace

PacketIoImpl::PacketIoImpl(
    std::unique_ptr<SystemCallAdapter> system_call_adapter,
    std::vector<std::unique_ptr<sonic::PacketIoPortSockets>> port_sockets)
    : system_call_adapter_(std::move(system_call_adapter)),
      port_sockets_(std::move(port_sockets)),
      port_to_socket_(CreateSocketMap(port_sockets_)) {}

PacketIoImpl::PacketIoImpl(
    std::unique_ptr<SystemCallAdapter> system_call_adapter,
    const packet_metadata::ReceiveCallbackFunction callback_function,
    const bool use_genetlink)
    : system_call_adapter_(std::move(system_call_adapter)),
      callback_function_(callback_function),
      use_genetlink_(use_genetlink) {}

absl::StatusOr<std::unique_ptr<PacketIoInterface>>
PacketIoImpl::CreatePacketIoImpl() {
  auto system_call_adapter = absl::make_unique<SystemCallAdapter>();
  auto port_sockets_or =
      p4rt_app::sonic::DiscoverPacketIoPorts(*system_call_adapter);
  if (!port_sockets_or.ok()) {
    return gutil::InternalErrorBuilder() << port_sockets_or.status();
  }
  auto packetio_impl = absl::make_unique<PacketIoImpl>(
      std::move(system_call_adapter), std::move(*port_sockets_or));
  return packetio_impl;
}

absl::Status PacketIoImpl::SendPacketOut(absl::string_view port_name,
                                         const std::string& packet) {
  // Retrieve the transmit socket for this egress port.
  ASSIGN_OR_RETURN(
      auto socket, gutil::FindOrStatus(port_to_socket_, std::string(port_name)),
      _ << "Unable to find transmit socket for destination: " << port_name);
  return sonic::SendPacketOut(*system_call_adapter_, socket, port_name, packet);
}

absl::StatusOr<std::thread> PacketIoImpl::StartReceive(
    packet_metadata::ReceiveCallbackFunction callback_function,
    bool use_genetlink) {
  LOG(INFO) << "Spawning Rx thread";
  if (use_genetlink) {
    return packet_metadata::StartReceive(callback_function);
  } else {
    return sonic::StartReceive(callback_function, port_to_socket_);
  }
}

absl::Status PacketIoImpl::AddPacketIoPort(absl::string_view port_name) {
  if (port_to_socket_.find(port_name) != port_to_socket_.end()) {
    // Already existing port, nothing to do.
    return absl::OkStatus();
  }

  // Nothing to do if this is not an interesting port(Ethernet* and
  // submit_to_ingress) for Packet I/O.
  if (!absl::StartsWith(port_name, "Ethernet") &&
      !absl::StartsWith(port_name, kSubmitToIngress)) {
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(auto port_params,
                   sonic::AddPacketIoPort(*system_call_adapter_, port_name,
                                          callback_function_));
  // Add the socket to transmit socket map.
  port_to_socket_[port_name] = port_params->socket;

  // Nothing more to do, if in genetlink receive mode.
  // PacketInSelectables is needed only for Netdev receive model.
  if (use_genetlink_) return absl::OkStatus();

  // Add the port object into the port select so that receive thread can start
  // monitoring for receive packets.
  port_select_.addSelectable(port_params->packet_in_selectable.get());
  if (bool success = port_to_selectables_
                         .insert({std::string(port_name),
                                  std::move(port_params->packet_in_selectable)})
                         .second;
      !success) {
    return gutil::InternalErrorBuilder()
           << "Packet In selectable already exists for this port: "
           << port_name;
  }
  return absl::OkStatus();
}

absl::Status PacketIoImpl::RemovePacketIoPort(absl::string_view port_name) {
  auto it = port_to_selectables_.find(port_name);
  if (it == port_to_selectables_.end()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unable to find selectables for port remove: ", port_name));
  }

  // Cleanup PacketInSelectable, if in Netdev mode.
  if (!use_genetlink_) {
    std::unique_ptr<sonic::PacketInSelectable>& port_selectable = it->second;
    // Remove the port selectable from the selectables object.
    port_select_.removeSelectable(port_selectable.get());
    if (port_to_selectables_.erase(port_name) != 1) {
      return gutil::InternalErrorBuilder()
             << "Unable to remove selectable for this port: " << port_name;
    }
  }

  ASSIGN_OR_RETURN(auto socket,
                   gutil::FindOrStatus(port_to_socket_, std::string(port_name)),
                   _ << "Unable to find port: " << port_name);
  if (socket >= 0) {
    close(socket);
  }
  port_to_socket_.erase(port_name);

  return absl::OkStatus();
}

}  // namespace sonic
}  // namespace p4rt_app

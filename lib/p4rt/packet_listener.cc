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
#include "lib/p4rt/packet_listener.h"

namespace pins_test {
PacketListener::PacketListener(
    pdpi::P4RuntimeSession* session, const pdpi::IrP4Info* ir_p4info,
    const absl::flat_hash_map<std::string, std::string>*
        interface_port_id_to_name,
    thinkit::PacketCallback callback)
    : session_(std::move(session)),
      receive_packet_thread_([this, ir_p4info, interface_port_id_to_name,
                              callback = std::move(callback)]() {
        p4::v1::StreamMessageResponse pi_response;
        while (session_->StreamChannelRead(pi_response)) {
          sai::StreamMessageResponse pd_response;
          if (!pdpi::PiStreamMessageResponseToPd(*ir_p4info, pi_response,
                                                 &pd_response)
                   .ok()) {
            LOG(ERROR) << "Failed to convert PI stream message response to PD.";
            return;
          }
          if (!pd_response.has_packet()) {
            LOG(ERROR) << "PD response has no packet.";
            return;
          }
          std::string port_id = pd_response.packet().metadata().ingress_port();
          auto port_name = interface_port_id_to_name->find(port_id);
          if (port_name == interface_port_id_to_name->end()) {
            LOG(WARNING) << port_id << " not found.";
            return;
          }
          callback(port_name->second,
                   absl::BytesToHexString(pd_response.packet().payload()));
        }
      }) {}

}  // namespace pins_test

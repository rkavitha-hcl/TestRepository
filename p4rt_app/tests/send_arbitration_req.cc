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
#include <arpa/inet.h>

#include <fstream>
#include <thread>  //NOLINT

#include "absl/time/time.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "gutil/status.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4_pdpi/utils/ir.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace {
const uint64_t kDeviceId = 183807201;

// Write request to insert an entry into the routing_nexthop_table, with
// nexthop_id as match field, and set_nexthop as action
constexpr char kWriteRequestFlow1[] = R"(
updates {
      type: INSERT
      entity {
        table_entry {
          table_id: 33554497
          match { field_id: 1 exact { value: "\000\000\000\001" } }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\000\001" }
              params { param_id: 2 value: "\000\002\003\004\005\005" }
            }
          }
        }
      }
}
updates {
      type: INSERT
      entity {
        table_entry {
          table_id: 33554497
          match { field_id: 1 exact { value: "\000\000\000\002" } }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\000\002" }
              params { param_id: 2 value: "\000\002\003\004\005\006" }
            }
          }
        }
      }
}
)";

constexpr char kWriteRequestFlow2[] = R"(
updates {
      type: INSERT
      entity {
        table_entry {
          table_id: 33554497
          match { field_id: 1 exact { value: "\000\000\000\003" } }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\000\003" }
              params { param_id: 2 value: "\000\002\003\004\005\005" }
            }
          }
        }
      }
}
updates {
      type: INSERT
      entity {
        table_entry {
          table_id: 33554497
          match { field_id: 1 exact { value: "\000\000\000\004" } }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\000\004" }
              params { param_id: 2 value: "\000\002\003\004\005\006" }
            }
          }
        }
      }
}
)";

constexpr char kWriteInValidRequestFlow[] = R"(
updates {
      type: INSERT
      entity {
        table_entry {
          table_id: 33554497
          match { field_id: 1 exact { value: "\000\000\000\001" } }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\000\070" }
              params { param_id: 2 value: "\000\002\003\004\005\007" }
            }
          }
        }
      }
}
updates {
      type: INSERT
      entity {
        table_entry {
          table_id: 33554497
          match { field_id: 1 exact { value: "\000\000\000\002" } }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\000\080" }
              params { param_id: 2 value: "\000\002\003\004\005\007" }
            }
          }
        }
      }
}
)";

static constexpr char kTestPacket[] =
    "\x02\x32\x00\x00\x00\x01\x00\x00\x00\x00\x00\x01\x81\x00\x00\x01\x08\x00"
    "\x45\x00\x00\x2d\x00\x01\x00\x00\x40\xfe\x62\xd1\x0a\x00\x01\x01\x0a\x00"
    "\x02\x01\x54\x65\x73\x74\x2c\x20\x54\x65\x73\x74\x2c\x20\x54\x65\x73\x74"
    "\x2c\x20\x54\x65\x73\x74\x21\x21\x21";

class P4rtControllerClient {
 public:
  P4rtControllerClient(p4::v1::Uint128 election_id)
      : election_id_(election_id) {}

  ~P4rtControllerClient() {
    if (receive_thread_.joinable()) {
      receive_thread_.detach();
    }
  }

  bool PushP4Info() {
    p4::v1::SetForwardingPipelineConfigRequest request;
    request.set_action(
        p4::v1::SetForwardingPipelineConfigRequest::VERIFY_AND_COMMIT);
    request.set_device_id(kDeviceId);
    *request.mutable_election_id() = GetElectionId();
    *request.mutable_config()->mutable_p4info() =
        sai::GetP4Info(sai::SwitchRole::kMiddleblock);

    p4::v1::SetForwardingPipelineConfigResponse response;
    grpc::ClientContext context;
    auto status =
        stub_->SetForwardingPipelineConfig(&context, request, &response);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to push P4Info, error: " << status.error_message();
      return false;
    }

    return true;
  }

  bool OpenStream() {
    channel_ =
        grpc::CreateChannel("[::]:9559", grpc::InsecureChannelCredentials());
    stub_ = p4::v1::P4Runtime::NewStub(channel_);
    stream_ = stub_->StreamChannel(&context_);
    if (!stream_) return false;
    return true;
  }

  bool SendArbitrationRequest() {
    p4::v1::StreamMessageRequest request;
    auto arbitration = request.mutable_arbitration();
    arbitration->set_device_id(kDeviceId);
    *arbitration->mutable_election_id() = election_id_;
    return stream_->Write(request);
  }

  absl::Status SendSampleStreamPacket(uint32_t egress_port_id) {
    p4::v1::StreamMessageRequest request;
    auto packet = request.mutable_packet();
    packet->set_payload(std::string(kTestPacket, sizeof(kTestPacket)));
    auto metadata = packet->add_metadata();
    metadata->set_metadata_id(
        PACKET_OUT_EGRESS_PORT_ID);  // metadata id for egress_port.
    ASSIGN_OR_RETURN(const auto port,
                     pdpi::UintToNormalizedByteString(
                         static_cast<uint64_t>(egress_port_id), 32));
    metadata->set_value(port);

    metadata = packet->add_metadata();
    metadata->set_metadata_id(
        PACKET_OUT_SUBMIT_TO_INGRESS_ID);  // metadata id for inject into
                                           // ingress stage, unused.
    ASSIGN_OR_RETURN(auto ingress, pdpi::UintToNormalizedByteString(
                                       static_cast<uint64_t>(0), 1));
    metadata->set_value(ingress);

    metadata = packet->add_metadata();
    metadata->set_metadata_id(
        PACKET_OUT_UNUSED_PAD_ID);  // metadata id for unused_pad, unused..
    ASSIGN_OR_RETURN(auto pad, pdpi::UintToNormalizedByteString(
                                   static_cast<uint64_t>(0), 7));
    metadata->set_value(pad);

    // Erros are written to the /tmp/stream.txt file.
    RET_CHECK(stream_->Write(request) == true)
        << "Stream write for PacketOut failed";

    return absl::OkStatus();
  }

  p4::v1::Uint128 GetElectionId() { return election_id_; }

  bool IsMaster() {
    p4::v1::StreamMessageResponse response;
    if (!stream_->Read(&response)) {
      LOG(ERROR) << "Failed to read response from stream";
      return false;
    }

    auto update = response.arbitration();
    if (update.status().code() != grpc::StatusCode::OK) return false;
    if (update.election_id().high() != election_id_.high() ||
        update.election_id().low() != election_id_.low())
      return false;
    return true;
  }

  bool IsSecondary(p4::v1::Uint128 master_election_id) {
    p4::v1::StreamMessageResponse response;
    if (!stream_->Read(&response)) {
      LOG(ERROR) << "Failed to read response from stream";
      return false;
    }

    auto update = response.arbitration();
    if (update.status().code() != grpc::StatusCode::NOT_FOUND &&
        update.status().code() != grpc::StatusCode::ALREADY_EXISTS)
      return false;
    if (update.election_id().high() != master_election_id.high() ||
        update.election_id().low() != master_election_id.low())
      return false;
    return true;
  }

  // Checks current client is secondary when no masters have came up at all.
  bool IsSecondary() {
    p4::v1::StreamMessageResponse response;
    if (!stream_->Read(&response)) {
      LOG(ERROR) << "Failed to read response from stream";
      return false;
    }
    return response.arbitration().status().code() != grpc::StatusCode::OK;
  }

  absl::Status SendProtoRequest(p4::v1::WriteRequest& request) {
    request.set_device_id(kDeviceId);
    request.set_role_id(0);
    *request.mutable_election_id() = election_id_;

    grpc::ClientContext context;
    p4::v1::WriteResponse response;
    auto status = stub_->Write(&context, request, &response);
    if (!status.ok()) {
      return gutil::InternalErrorBuilder()
             << "Received error status " << status.error_message();
    }
    return absl::OkStatus();
  }

  absl::Status SendRequest(absl::string_view request_str) {
    p4::v1::WriteRequest request;
    if (!google::protobuf::TextFormat::ParseFromString(std::string(request_str),
                                                       &request)) {
      return gutil::InvalidArgumentErrorBuilder()
             << "Couldn't parse " << request_str << " WriteRequest protobuf.";
    }
    return SendProtoRequest(request);
  }

  absl::Status SendWriteRequest() {
    for (const auto& request_type :
         {kWriteRequestFlow1, kWriteInValidRequestFlow, kWriteRequestFlow2}) {
      auto result = SendRequest(request_type);
      if (!result.ok()) {
        LOG(ERROR) << "Write request " << request_type
                   << "failed with error: " << result.message();
      }
    }
    return absl::OkStatus();
  }

  void SpawnReceiveThread() {
    auto ReceivePackets = [this]() -> void {
      p4::v1::StreamMessageResponse response;
      std::ofstream out_file;
      out_file.open("/tmp/stream.txt");
      // Go into a continous loop to receive packets.
      while (1) {
        if (!stream_->Read(&response)) {
          out_file << "Failed to read response from stream" << std::endl;
        } else {
          out_file << "Recieved packet " << response.DebugString();
        }
        out_file.flush();
      }
    };

    receive_thread_ = std::thread(ReceivePackets);
    LOG(INFO) << "Master stream messages are written to /tmp/stream.txt";
  }

 private:
  p4::v1::Uint128 election_id_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<p4::v1::P4Runtime::Stub> stub_;
  grpc::ClientContext context_;
  std::unique_ptr<::grpc::ClientReaderWriter<p4::v1::StreamMessageRequest,
                                             p4::v1::StreamMessageResponse>>
      stream_;
  std::thread receive_thread_;
};

p4::v1::Uint128 IntToElectionId(int id) {
  p4::v1::Uint128 election_id;
  election_id.set_high(id);
  election_id.set_low(id);
  return election_id;
}

}  // namespace

enum Options {
  SecondaryArbitrationReq = 1,
  MasterArbitrationReq = 2,
  MasterSendPackets = 3,
  SecondarySendPackets = 4,
  SendWriteRequest = 5,
  Quit = 6,
};

int SendSecondaryArbitrationRequest(
    std::shared_ptr<P4rtControllerClient> secondary) {
  if (!secondary->OpenStream()) {
    LOG(ERROR) << "Failed to open stream channel";
    return 0;
  }

  if (!secondary->SendArbitrationRequest()) {
    LOG(ERROR) << "Failed to send arbitration request";
    return 0;
  }

  return 1;
}

int SendMasterArbitrationRequest(std::shared_ptr<P4rtControllerClient> master) {
  if (!master->OpenStream()) {
    LOG(ERROR) << "Failed to open stream channel";
    return 0;
  }

  if (!master->SendArbitrationRequest()) {
    LOG(ERROR) << "Failed to send arbitration request";
    return 0;
  }

  if (!master->IsMaster()) {
    LOG(ERROR) << "Failed to become master";
  }

  if (!master->PushP4Info()) {
    return 0;
  }

  master->SpawnReceiveThread();

  return 1;
}

void ControllerSendPackets(P4rtControllerClient* controller, bool is_master) {
  if (controller == nullptr) {
    LOG(ERROR) << "[Test send PacketOuts] Controller not available for "
                  "sending packets yet.";
    return;
  }
  int controller_port_no, start, end;
  std::cout << "Enter egress controller port : ";
  std::cin >> controller_port_no;
  if (controller_port_no == -1) {
    // all ports.
    start = 0;
    end = 252;
  } else {
    start = end = controller_port_no;
  }
  int num_packets;
  std::cout << "Enter number of packets : ";
  std::cin >> num_packets;
  for (; start <= end; start += 4) {
    for (int i = 0; i < num_packets; i++) {
      auto status = controller->SendSampleStreamPacket(start);
      if (!status.ok()) {
        if (is_master) {
          LOG(ERROR)
              << "[Test send PacketOuts] Unable to send packet out for port "
              << start << "Error : " << status.ToString();
        } else {
          LOG(ERROR) << "[Test send PacketOuts] Expected: non-master "
                        "controller is restricted "
                        "from sending packets.";
        }
      } else {
        if (!is_master) {
          LOG(ERROR) << "[Test send PacketOuts] Error: non-master controller "
                        "should not be able to send "
                        "packets. Packet sent out for port "
                     << start;
        } else {
          LOG(INFO) << "Packet Out sent successfully";
        }
      }
    }
  }
}

void DisplayMenu() {
  std::cout << "1. Secondary Abritration Request" << std::endl;
  std::cout << "2. Master Abritration Request" << std::endl;
  std::cout << "3. Send Packet Out via Master" << std::endl;
  std::cout << "4. Send Packet Out via Secondary Controller" << std::endl;
  std::cout << "5. Write Request" << std::endl;
  std::cout << "6. Quit" << std::endl;
  std::cout << "Enter an option : ";
}

int main() {
  std::shared_ptr<P4rtControllerClient> secondary;
  std::shared_ptr<P4rtControllerClient> master;
  int option = 0;
  while (1) {
    DisplayMenu();
    std::cin >> option;
    switch (option) {
      case SecondaryArbitrationReq: {
        secondary = std::make_shared<P4rtControllerClient>(IntToElectionId(0));
        if (SendSecondaryArbitrationRequest(secondary)) {
          LOG(INFO) << "Secondary Arb Request sent successfully"
                    << secondary.use_count();
          // Checks current client is secondary when no masters have came up
          // at all.
          if (master == nullptr) {
            if (!secondary->IsSecondary()) {
              LOG(ERROR) << "Controller client with election_id 0 is not "
                            "allowed to be master";
            }
          }
        } else {
          LOG(ERROR) << "Secondary Arb Request failed";
          secondary.reset();
        }
        break;
      }
      case MasterArbitrationReq: {
        master = std::make_shared<P4rtControllerClient>(IntToElectionId(1));
        if (SendMasterArbitrationRequest(master)) {
          LOG(INFO) << "Master Arb Request sent successfully";
          if (secondary) {
            if (!secondary->IsSecondary(master->GetElectionId())) {
              LOG(ERROR) << "Failed to send out advisory message for "
                            "mastership change";
            }
          }
        } else {
          LOG(ERROR) << "Master Arb Request failed";
          master.reset();
        }
        break;
      }
      case MasterSendPackets: {
        ControllerSendPackets(master.get(), /*is_master=*/true);
        break;
      }
      case SecondarySendPackets: {
        ControllerSendPackets(secondary.get(), /*is_master=*/false);
        break;
      }
      case SendWriteRequest: {
        // Generate a write request.
        if (secondary) {
          auto result = secondary->SendWriteRequest();
          if (result.ok()) {
            LOG(ERROR) << "Non-master controllers should not be able to write";
          } else {
            LOG(INFO) << "[EXPECTED] Secondary controller not permitted to "
                         "write to switch";
          }
        } else {
          LOG(ERROR) << "Secondary not available yet";
        }
        if (master) {
          auto result = master->SendWriteRequest();
          if (!result.ok()) {
            LOG(ERROR) << "Master controller unable to write"
                       << result.message();
          } else {
            LOG(INFO) << "[EXPECTED] Master controller wrote to switch";
          }
        } else {
          LOG(ERROR) << "Master not available yet";
        }
        break;
      }
      case Quit: {
        LOG(INFO) << "Quitting";
        return 0;
      }
      default: {
        LOG(INFO) << "Wrong option";
        break;
      }
    }  // switch
  }    // while (1)
}

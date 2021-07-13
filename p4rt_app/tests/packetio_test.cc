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
#include <thread>  //NOLINT

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/pd.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "p4rt_app/sonic/fake_packetio_interface.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"

namespace p4rt_app {
namespace {

using ::google::protobuf::TextFormat;
using ::p4::v1::P4Runtime;
using ::testing::Eq;

// Test class for PacketIo component tests.
class FakePacketIoTest : public testing::Test {
 protected:
  void SetUp() override {
    const std::string address =
        absl::StrCat("localhost:", p4rt_service_.GrpcPort());
    auto stub =
        pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_, pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));

    p4rt_service_.GetPortAppDbTable().InsertTableEntry("Ethernet0",
                                                       {{"id", "0"}});
    p4rt_service_.GetPortAppDbTable().InsertTableEntry("Ethernet1",
                                                       {{"id", "1"}});
  }

  // Form PacketOut message and write to stream channel.
  absl::Status SendPacketOut(int port, absl::string_view data,
                             const pdpi::IrP4Info& p4info) {
    // Assemble PacketOut protobuf message.
    sai::PacketOut packet_out;
    packet_out.set_payload(std::string(data));
    sai::PacketOut::Metadata& metadata = *packet_out.mutable_metadata();
    metadata.set_egress_port(absl::StrCat(port));
    metadata.set_submit_to_ingress(pdpi::BitsetToHexString<1>(0));
    metadata.set_unused_pad(pdpi::BitsetToHexString<7>(0));

    // Assemble PacketOut request and write to stream channel.
    p4::v1::StreamMessageRequest request;
    ASSIGN_OR_RETURN(*request.mutable_packet(),
                     pdpi::PdPacketOutToPi(p4info, packet_out));
    RET_CHECK(p4rt_session_->StreamChannelWrite(request));
    return absl::OkStatus();
  }

  // Helper method to read Responses from stream channel.
  static void ReadResponses(FakePacketIoTest* const fake_test,
                            int expected_count) {
    pdpi::P4RuntimeSession* const p4rt_session = fake_test->p4rt_session_.get();
    p4::v1::StreamMessageResponse response;
    int i = 0;
    while (i < expected_count && p4rt_session->StreamChannelRead(response)) {
      if (response.has_error()) {
        LOG(ERROR) << "Received error on stream channel: "
                   << response.DebugString();
      }
      fake_test->actual_responses_.push_back(response);
      i++;
    }
  }

  test_lib::P4RuntimeGrpcService p4rt_service_ =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
  std::vector<p4::v1::StreamMessageResponse> actual_responses_;
};

TEST_F(FakePacketIoTest, VerifyPacketIn) {
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      sai::GetP4Info(sai::Instantiation::kMiddleblock)));

  std::vector<p4::v1::PacketIn> expected_packets;
  expected_packets.resize(2);
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(payload: "test packet1"
           metadata { metadata_id: 1 value: "\000\000" }
           metadata { metadata_id: 2 value: "\000\000" }
      )pb",
      &expected_packets.at(0)));
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(payload: "test packet2"
           metadata { metadata_id: 1 value: "\000\001" }
           metadata { metadata_id: 2 value: "\000\001" }
      )pb",
      &expected_packets.at(1)));

  // Spawn the receiver thread.
  std::thread receive_thread(&FakePacketIoTest::ReadResponses, this,
                             /*expected_count=*/2);
  // Push the expected PacketIn.
  EXPECT_OK(p4rt_service_.GetFakePacketIoInterface().PushPacketIn(
      "Ethernet0", "Ethernet0", "test packet1"));
  EXPECT_OK(p4rt_service_.GetFakePacketIoInterface().PushPacketIn(
      "Ethernet1", "Ethernet1", "test packet2"));
  // Retry a few times to check if all expected packets arrived.
  for (int i = 0; i < 10; i++) {
    if (actual_responses_.size() == 2) {
      break;
    } else {
      absl::SleepFor(absl::Seconds(1));
    }
  }
  // ASSERT_EQ will ensure either the receive thread is joined or killed when
  // this test finishes.
  ASSERT_EQ(actual_responses_.size(), 2);
  ASSERT_TRUE(std::all_of(actual_responses_.begin(), actual_responses_.end(),
                          [](const p4::v1::StreamMessageResponse& response) {
                            return response.has_packet();
                          }));
  receive_thread.join();
}

TEST_F(FakePacketIoTest, PacketOutFailBeforeP4InfoPush) {
  std::thread receive_thread(&FakePacketIoTest::ReadResponses, this,
                             /*expected_count=*/1);
  EXPECT_OK(SendPacketOut(0, "test packet1",
                          sai::GetIrP4Info(sai::Instantiation::kMiddleblock)));
  // Retry a few times to check if the expected error arrived.
  for (int i = 0; i < 10; i++) {
    if (actual_responses_.size() == 1) {
      break;
    } else {
      absl::SleepFor(absl::Seconds(1));
    }
  }
  ASSERT_EQ(actual_responses_.size(), 1);
  ASSERT_TRUE(actual_responses_[0].has_error());
  ASSERT_THAT(actual_responses_[0].error().canonical_code(),
              Eq(grpc::StatusCode::FAILED_PRECONDITION));
  receive_thread.join();
}

TEST_F(FakePacketIoTest, PacketOutFailForSecondary) {
  // Assemble PacketOut protobuf message.
  sai::PacketOut packet_out;
  packet_out.set_payload(std::string("test packet"));
  sai::PacketOut::Metadata& metadata = *packet_out.mutable_metadata();
  metadata.set_egress_port(pdpi::BitsetToHexString<9>(/*bitset=*/0));
  metadata.set_submit_to_ingress(pdpi::BitsetToHexString<1>(0));
  metadata.set_unused_pad(pdpi::BitsetToHexString<7>(0));

  // Assemble PacketOut request and write to stream channel.
  p4::v1::StreamMessageRequest request;
  ASSERT_OK_AND_ASSIGN(
      *request.mutable_packet(),
      pdpi::PdPacketOutToPi(sai::GetIrP4Info(sai::Instantiation::kMiddleblock),
                            packet_out));
  std::string address = absl::StrCat("localhost:", p4rt_service_.GrpcPort());
  auto channel =
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
  auto stub = P4Runtime::NewStub(channel);
  grpc::ClientContext context;
  std::unique_ptr<::grpc::ClientReaderWriter<p4::v1::StreamMessageRequest,
                                             p4::v1::StreamMessageResponse>>
      stream = stub->StreamChannel(&context);
  ASSERT_TRUE(stream->Write(request));
  // Wait for a response.
  p4::v1::StreamMessageResponse response;
  ASSERT_TRUE(stream->Read(&response)) << "Did not receive stream response: "
                                       << stream->Finish().error_message();

  ASSERT_TRUE(response.has_error());
  ASSERT_THAT(response.error().canonical_code(),
              Eq(grpc::StatusCode::PERMISSION_DENIED));
}

TEST_F(FakePacketIoTest, VerifyPacketOut) {
  // Needed for PacketOut.
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      sai::GetP4Info(sai::Instantiation::kMiddleblock)));

  EXPECT_OK(SendPacketOut(0, "test packet1",
                          sai::GetIrP4Info(sai::Instantiation::kMiddleblock)));
  EXPECT_OK(SendPacketOut(0, "test packet2",
                          sai::GetIrP4Info(sai::Instantiation::kMiddleblock)));

  absl::StatusOr<std::vector<std::string>> packets_or;
  // Retry for a few times with delay since it takes a few msecs for Write
  // rpc call to reach the P4RT server and the write request processed.
  for (int i = 0; i < 10; i++) {
    packets_or =
        p4rt_service_.GetFakePacketIoInterface().VerifyPacketOut("Ethernet0");
    if (!packets_or.ok() || (*packets_or).size() != 2) {
      absl::SleepFor(absl::Seconds(2));
    } else {
      break;
    }
  }
  ASSERT_OK(packets_or);
  EXPECT_EQ(*packets_or,
            std::vector<std::string>({"test packet1", "test packet2"}));
}

}  // namespace
}  // namespace p4rt_app

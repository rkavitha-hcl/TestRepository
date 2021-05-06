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

#include <memory>

#include "absl/status/status.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"

namespace p4rt_app {
namespace {

using P4RuntimeStream =
    ::grpc::ClientReaderWriter<p4::v1::StreamMessageRequest,
                               p4::v1::StreamMessageResponse>;

p4::v1::Uint128 GetElectionId(int value) {
  p4::v1::Uint128 election_id;
  election_id.set_high(value);
  election_id.set_low(0);
  return election_id;
}

absl::StatusOr<p4::v1::StreamMessageResponse> GetStreamResponse(
    P4RuntimeStream& stream) {
  p4::v1::StreamMessageResponse response;
  if (!stream.Read(&response)) {
    return gutil::InternalErrorBuilder() << "Did not receive stream response: "
                                         << stream.Finish().error_message();
  }
  return response;
}

absl::StatusOr<p4::v1::StreamMessageResponse> SendStreamRequest(
    P4RuntimeStream& stream, const p4::v1::StreamMessageRequest& request) {
  stream.Write(request);
  return GetStreamResponse(stream);
}

class ArbitrationTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("localhost:", p4rt_service_.GrpcPort());
    auto channel =
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    LOG(INFO) << "Creating P4Runtime::Stub for " << address << ".";
    stub_ = p4::v1::P4Runtime::NewStub(channel);
  }

  int GetDeviceId() const { return 183807201; }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<p4::v1::P4Runtime::Stub> stub_;
};

TEST_F(ArbitrationTest, PrimaryConnectionWithElectionId) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  // Send only 1 arbitration request.
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  *request.mutable_arbitration()->mutable_election_id() = GetElectionId(1);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));

  // Because only 1 request was sent it should be the primary connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);
}

TEST_F(ArbitrationTest, PrimaryConnectionWithElectionIdZero) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  request.mutable_arbitration()->mutable_election_id()->set_high(0);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);
}

TEST_F(ArbitrationTest, NoElectionIdIsAlwaysBackupConnection) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));
  EXPECT_EQ(response.arbitration().status().code(),
            grpc::StatusCode::NOT_FOUND);
}

TEST_F(ArbitrationTest, PrimaryAndBackupConnections) {
  grpc::ClientContext context0;
  std::unique_ptr<P4RuntimeStream> stream0 = stub_->StreamChannel(&context0);

  grpc::ClientContext context1;
  std::unique_ptr<P4RuntimeStream> stream1 = stub_->StreamChannel(&context1);

  // Send first arbitration request.
  p4::v1::StreamMessageRequest request0;
  request0.mutable_arbitration()->set_device_id(GetDeviceId());
  *request0.mutable_arbitration()->mutable_election_id() = GetElectionId(2);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream0, request0));

  // Because it's the first request it will default to the primary connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

  // Send second arbitration request with a lower election ID.
  p4::v1::StreamMessageRequest request1;
  request1.mutable_arbitration()->set_device_id(GetDeviceId());
  *request1.mutable_arbitration()->mutable_election_id() = GetElectionId(1);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream1, request1));

  // Because the election ID is lower than the first this becomes the backup
  // connection.
  EXPECT_EQ(response.arbitration().status().code(),
            grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(ArbitrationTest, PrimaryConnectionCanBeReplacedByNewConnection) {
  grpc::ClientContext context0;
  std::unique_ptr<P4RuntimeStream> stream0 = stub_->StreamChannel(&context0);

  grpc::ClientContext context1;
  std::unique_ptr<P4RuntimeStream> stream1 = stub_->StreamChannel(&context1);

  // Send first arbitration request.
  p4::v1::StreamMessageRequest request0;
  request0.mutable_arbitration()->set_device_id(GetDeviceId());
  *request0.mutable_arbitration()->mutable_election_id() = GetElectionId(2);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream0, request0));

  // Because it's the first request it will default to the primary connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

  // Send second arbitration request with a higher election ID.
  p4::v1::StreamMessageRequest request1;
  request1.mutable_arbitration()->set_device_id(GetDeviceId());
  *request1.mutable_arbitration()->mutable_election_id() = GetElectionId(3);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream1, request1));

  // Because the election ID is higher than the first this becomes the new
  // primary connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

  // Because the primary connection changed we expect all connections to be
  // informed.
  ASSERT_OK_AND_ASSIGN(response, GetStreamResponse(*stream0));
  EXPECT_EQ(response.arbitration().status().code(),
            grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(ArbitrationTest, PrimaryConnectionCanReestablishAfterGoingDown) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  // Send first arbitration request.
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  *request.mutable_arbitration()->mutable_election_id() = GetElectionId(2);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));

  // Because it's the first request it will default to the primary connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

  // Close the stream to flush the connection for the P4RT service.
  stream->WritesDone();
  EXPECT_OK(stream->Finish());

  // Then open a new one, and send the same arbitration request.
  grpc::ClientContext new_context;
  stream = stub_->StreamChannel(&new_context);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream, request));

  // Because the old stream was flushed we can re-establish the connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);
}

TEST_F(ArbitrationTest, PrimaryCanSendDuplicateArbitationRequests) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  // Send first arbitration request.
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  *request.mutable_arbitration()->mutable_election_id() = GetElectionId(2);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));

  // Because it's the first request it will default to the primary connection.
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

  // Sending a duplicate request is effectivly a no-op, and the switch should
  // still return that it's the primary connection.
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream, request));
  EXPECT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);
}

TEST_F(ArbitrationTest, BackupConnectionCannotUpdateForwardingPipeline) {
  grpc::ClientContext stream_context;
  std::unique_ptr<P4RuntimeStream> stream =
      stub_->StreamChannel(&stream_context);

  // Test with forced backup connection.
  {
    p4::v1::StreamMessageRequest request;
    request.mutable_arbitration()->set_device_id(GetDeviceId());
    ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                         SendStreamRequest(*stream, request));
    ASSERT_EQ(response.arbitration().status().code(),
              grpc::StatusCode::NOT_FOUND);
  }

  p4::v1::SetForwardingPipelineConfigRequest request;
  request.set_device_id(GetDeviceId());

  p4::v1::SetForwardingPipelineConfigResponse response;
  grpc::ClientContext context;
  EXPECT_EQ(stub_->SetForwardingPipelineConfig(&context, request, &response)
                .error_code(),
            grpc::StatusCode::PERMISSION_DENIED);
}

TEST_F(ArbitrationTest, BackupConnectionCannotSendWriteRequest) {
  grpc::ClientContext primary_context;
  std::unique_ptr<P4RuntimeStream> primary =
      stub_->StreamChannel(&primary_context);

  grpc::ClientContext backup_context;
  std::unique_ptr<P4RuntimeStream> backup =
      stub_->StreamChannel(&backup_context);

  // Test with primary & backup connection.
  {
    p4::v1::StreamMessageResponse response;
    p4::v1::StreamMessageRequest request;
    request.mutable_arbitration()->set_device_id(GetDeviceId());
    request.mutable_arbitration()->mutable_election_id()->set_high(2);
    ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*primary, request));
    ASSERT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

    request.mutable_arbitration()->mutable_election_id()->set_high(1);
    ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*backup, request));
    ASSERT_EQ(response.arbitration().status().code(),
              grpc::StatusCode::ALREADY_EXISTS);
  }

  p4::v1::WriteRequest request;
  request.set_device_id(GetDeviceId());
  request.mutable_election_id()->set_high(1);

  p4::v1::WriteResponse response;
  grpc::ClientContext context;
  EXPECT_EQ(stub_->Write(&context, request, &response).error_code(),
            grpc::StatusCode::PERMISSION_DENIED);
}

// Only applies if they are the same role.
TEST_F(ArbitrationTest, TwoConnectionsCannotReuseElectionId) {
  grpc::ClientContext primary_context;
  std::unique_ptr<P4RuntimeStream> primary =
      stub_->StreamChannel(&primary_context);

  grpc::ClientContext backup_context;
  std::unique_ptr<P4RuntimeStream> backup =
      stub_->StreamChannel(&backup_context);

  p4::v1::StreamMessageResponse response;
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  request.mutable_arbitration()->mutable_election_id()->set_high(2);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*primary, request));
  ASSERT_EQ(response.arbitration().status().code(), grpc::StatusCode::OK);

  request.mutable_arbitration()->mutable_election_id()->set_high(2);
  backup->Write(request);
  EXPECT_EQ(backup->Finish().error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace
}  // namespace p4rt_app

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

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"

namespace p4rt_app {
namespace {

using ::p4::v1::P4Runtime;
using ::testing::Eq;

using P4RuntimeStream =
    ::grpc::ClientReaderWriter<p4::v1::StreamMessageRequest,
                               p4::v1::StreamMessageResponse>;

p4::v1::Uint128 GetElectionId(int value) {
  p4::v1::Uint128 election_id;
  election_id.set_high(value);
  election_id.set_low(0);
  return election_id;
}

class ArbitrationTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("127.0.0.1:", p4rt_service_.GrpcPort());
    auto channel =
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    LOG(INFO) << "Creating P4Runtime::Stub for " << address << ".";
    stub_ = P4Runtime::NewStub(channel);
  }

  int GetDeviceId() const { return 183807201; }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<P4Runtime::Stub> stub_;
};

absl::StatusOr<p4::v1::StreamMessageResponse> SendStreamRequest(
    P4RuntimeStream& stream, const p4::v1::StreamMessageRequest& request) {
  // Issue the request.
  stream.Write(request);

  // Wait for a response.
  p4::v1::StreamMessageResponse response;
  if (!stream.Read(&response)) {
    return gutil::InternalErrorBuilder() << "Did not receive stream response: "
                                         << stream.Finish().error_message();
  }
  return response;
}

TEST_F(ArbitrationTest, EstablishPrimaryConnection) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  // Send only 1 arbitration request.
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  *request.mutable_arbitration()->mutable_election_id() = GetElectionId(1);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));

  // Because only 1 request was sent it should be the primary connection.
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));
}

TEST_F(ArbitrationTest, EstablishSecondaryConnection) {
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
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));

  // Send second arbitration request with a lower election ID.
  p4::v1::StreamMessageRequest request1;
  request1.mutable_arbitration()->set_device_id(GetDeviceId());
  *request1.mutable_arbitration()->mutable_election_id() = GetElectionId(1);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream1, request1));

  // Because the election ID is lower than the first this becomes the secondary
  // connection.
  EXPECT_THAT(response.arbitration().status().code(),
              Eq(grpc::StatusCode::ALREADY_EXISTS));
}

TEST_F(ArbitrationTest, ReplacePrimaryConnection) {
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
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));

  // Send second arbitration request with a higher election ID.
  p4::v1::StreamMessageRequest request1;
  request1.mutable_arbitration()->set_device_id(GetDeviceId());
  *request1.mutable_arbitration()->mutable_election_id() = GetElectionId(3);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream1, request1));

  // Because the election ID is higher than the first this becomes the new
  // primary connection.
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));
}

TEST_F(ArbitrationTest, ReestablishPrimaryConnection) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  // Send first arbitration request.
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  *request.mutable_arbitration()->mutable_election_id() = GetElectionId(2);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));

  // Because it's the first request it will default to the primary connection.
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));

  // Close the stream to flush the connection for the P4RT service.
  stream->WritesDone();
  EXPECT_OK(stream->Finish());

  // Then open a new one, and send the same arbitration request.
  grpc::ClientContext new_context;
  stream = stub_->StreamChannel(&new_context);
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream, request));

  // Because the old stream was flushed we can re-establish the connection.
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));
}

TEST_F(ArbitrationTest, DuplicateArbitationRequests) {
  grpc::ClientContext context;
  std::unique_ptr<P4RuntimeStream> stream = stub_->StreamChannel(&context);

  // Send first arbitration request.
  p4::v1::StreamMessageRequest request;
  request.mutable_arbitration()->set_device_id(GetDeviceId());
  *request.mutable_arbitration()->mutable_election_id() = GetElectionId(2);
  ASSERT_OK_AND_ASSIGN(p4::v1::StreamMessageResponse response,
                       SendStreamRequest(*stream, request));

  // Because it's the first request it will default to the primary connection.
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));

  // Sending a duplicate request is effectivly a no-op, and the switch should
  // still return that it's the primary connection.
  ASSERT_OK_AND_ASSIGN(response, SendStreamRequest(*stream, request));
  EXPECT_THAT(response.arbitration().status().code(), Eq(grpc::StatusCode::OK));
}

}  // namespace
}  // namespace p4rt_app

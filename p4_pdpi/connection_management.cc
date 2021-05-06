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

#include "p4_pdpi/connection_management.h"

#include <string>

#include "absl/status/statusor.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "gutil/status.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "thinkit/switch.h"

namespace pdpi {
using ::p4::v1::P4Runtime;

// Create P4Runtime Stub.
std::unique_ptr<P4Runtime::Stub> CreateP4RuntimeStub(
    const std::string& address,
    const std::shared_ptr<grpc::ChannelCredentials>& credentials) {
  return P4Runtime::NewStub(grpc::CreateCustomChannel(
      address, credentials, GrpcChannelArgumentsForP4rt()));
}

// Creates a session with the switch, which lasts until the session object is
// destructed.
absl::StatusOr<std::unique_ptr<P4RuntimeSession>> P4RuntimeSession::Create(
    std::unique_ptr<P4Runtime::Stub> stub, uint32_t device_id,
    const P4RuntimeSessionOptionalArgs& metadata) {
  // Open streaming channel.
  // Using `new` to access a private constructor.
  std::unique_ptr<P4RuntimeSession> session =
      absl::WrapUnique(new P4RuntimeSession(
          device_id, std::move(stub), metadata.election_id, metadata.role));

  // Send arbitration request.
  p4::v1::StreamMessageRequest request;
  auto arbitration = request.mutable_arbitration();
  arbitration->set_device_id(device_id);
  arbitration->mutable_role()->set_name(metadata.role);
  *arbitration->mutable_election_id() = session->election_id_;
  if (!session->stream_channel_->Write(request)) {
    return gutil::UnavailableErrorBuilder()
           << "Unable to initiate P4RT connection to device ID " << device_id
           << "; gRPC stream channel closed.";
  }

  // Wait for arbitration response.
  p4::v1::StreamMessageResponse response;
  if (!session->stream_channel_->Read(&response)) {
    return gutil::InternalErrorBuilder()
           << "No arbitration response received because: "
           << gutil::GrpcStatusToAbslStatus(session->stream_channel_->Finish())
           << " with response: " << response.ShortDebugString();
  }
  if (response.update_case() != p4::v1::StreamMessageResponse::kArbitration) {
    return gutil::InternalErrorBuilder()
           << "No arbitration update received but received the update of "
           << response.update_case() << ": " << response.ShortDebugString();
  }
  if (response.arbitration().device_id() != session->device_id_) {
    return gutil::InternalErrorBuilder() << "Received device id doesn't match: "
                                         << response.ShortDebugString();
  }
  // TODO Enable this check once p4rt app supports role.
  // if (response.arbitration().role().name() != session->role_) {
  //   return gutil::InternalErrorBuilder() << "Received role doesn't match: "
  //                                        << response.ShortDebugString();
  // }
  if (response.arbitration().election_id().high() !=
      session->election_id_.high()) {
    return gutil::InternalErrorBuilder()
           << "Highest 64 bits of received election id doesn't match: "
           << response.ShortDebugString();
  }
  if (response.arbitration().election_id().low() !=
      session->election_id_.low()) {
    return gutil::InternalErrorBuilder()
           << "Lowest 64 bits of received election id doesn't match: "
           << response.ShortDebugString();
  }

  // When object returned doesn't have the same type as the function's return
  // type (i.e. unique_ptr vs StatusOr in this case), certain old compilers
  // won't implicitly wrap the return expressions in std::move(). Then, the case
  // here will trigger the copy of the unique_ptr, which is invalid. Thus, we
  // need to explicitly std::move the returned object here.
  // See:go/totw/labs/should-i-return-std-move.
  return std::move(session);
}

// Creates a session with the switch, which lasts until the session object is
// destructed.
absl::StatusOr<std::unique_ptr<P4RuntimeSession>> P4RuntimeSession::Create(
    const std::string& address,
    const std::shared_ptr<grpc::ChannelCredentials>& credentials,
    uint32_t device_id, const P4RuntimeSessionOptionalArgs& metadata) {
  return Create(CreateP4RuntimeStub(address, credentials), device_id, metadata);
}

absl::StatusOr<std::unique_ptr<P4RuntimeSession>> P4RuntimeSession::Create(
    thinkit::Switch& thinkit_switch,
    const P4RuntimeSessionOptionalArgs& metadata) {
  ASSIGN_OR_RETURN(auto stub, thinkit_switch.CreateP4RuntimeStub());
  return Create(std::move(stub), thinkit_switch.DeviceId(), metadata);
}

// Create the default session with the switch.
std::unique_ptr<P4RuntimeSession> P4RuntimeSession::Default(
    std::unique_ptr<P4Runtime::Stub> stub, uint32_t device_id,
    const std::string& role) {
  // Using `new` to access a private constructor.
  return absl::WrapUnique(
      new P4RuntimeSession(device_id, std::move(stub), device_id, role));
}
}  // namespace pdpi

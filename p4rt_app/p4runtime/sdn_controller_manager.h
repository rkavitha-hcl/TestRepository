/*
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _SDN_CONTROLLER_MANAGER_H_
#define _SDN_CONTROLLER_MANAGER_H_

#include "absl/container/flat_hash_map.h"
#include "p4/v1/p4runtime.grpc.pb.h"

namespace p4rt_app {

absl::uint128 ToNativeUint128(const p4::v1::Uint128 id);

// A connection between a controller and p4rt server.
class SdnControllerConnection {
 public:
  SdnControllerConnection(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<p4::v1::StreamMessageResponse,
                               p4::v1::StreamMessageRequest>* stream)
      : context_(context), stream_(stream) {}

  // Sends back StreamMessageResponse to this controller.
  void SendStreamMessageResponse(const p4::v1::StreamMessageResponse& response);

  absl::optional<absl::uint128> GetElectionId() const { return election_id_; }

 private:
  // Returns true if the controller has sent an arbitration message with an
  // election ID, device ID and role ID.  If false, then this controller
  // connection is necessarily a secondary connection.
  bool IsInitialized() const { return election_id_.has_value(); }

  // Describes an SDN controller using the election ID. P4RT normally also uses
  // the device ID and role ID, but the P4RT app here only supports a single
  // device and the default role, which is why it is not stored here.
  absl::optional<absl::uint128> election_id_;
  grpc::ServerContext* context_;
  grpc::ServerReaderWriter<p4::v1::StreamMessageResponse,
                           p4::v1::StreamMessageRequest>* stream_;

  friend class SdnControllerManager;
};

class SdnControllerManager {
 public:
  // TODO: Set device ID through gNMI.
  SdnControllerManager() : device_id_(183807201) {}

  grpc::Status HandleArbitrationUpdate(
      const p4::v1::MasterArbitrationUpdate& update,
      SdnControllerConnection* controller) ABSL_LOCKS_EXCLUDED(lock_);
  // G3_WARN : ABSL_EXCLUSIVE_LOCKS_REQUIRED(P4RuntimeImpl::server_state_lock_);

  void HandleControllerDisconnect(SdnControllerConnection* controller)
      ABSL_LOCKS_EXCLUDED(lock_);
  // G3_WARN ABSL_EXCLUSIVE_LOCKS_REQUIRED(P4RuntimeImpl::server_state_lock_);

  bool SendStreamMessageToMaster(const p4::v1::StreamMessageResponse& response)
      ABSL_LOCKS_EXCLUDED(lock_);

  bool IsMasterElectionId(absl::uint128 election_id) ABSL_LOCKS_EXCLUDED(lock_);

 private:
  SdnControllerConnection* GetMasterController() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  void UpdateAndSendResponse(SdnControllerConnection* controller,
                             absl::uint128 new_election_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  p4::v1::StreamMessageResponse PopulateMasterArbitrationResponse(
      bool is_master) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Sends out MasterArbitrationUpdate response to all connected controllers
  // (which has the same (device_id, role_id) pair) when mastership changes.
  void BroadCastMasterChangeUpdate() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  bool IsElectionIdInUse(absl::uint128 election_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  bool HasMasterController() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Based on P4Runtime spec, unspecified election_id (with high=0, low=0) is
  // considered to be lower than any election_id, hence a controller with
  // unspecified election_id can never become master. If highest_election_id_ is
  // 0, then no master controller has came up yet.
  bool IsMaster(SdnControllerConnection* controller)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    if (!controller->IsInitialized()) return false;
    return highest_election_id_ != 0 &&
           controller->election_id_.value() == highest_election_id_;
  }

  uint64_t device_id_;
  // Lock for protecting SdnControllerManager member fields.
  absl::Mutex lock_;
  // The highest election_id every received.
  absl::uint128 highest_election_id_ ABSL_GUARDED_BY(lock_) = 0;
  // All controllers that currently have an open stream channel.
  std::vector<SdnControllerConnection*> controllers_ ABSL_GUARDED_BY(lock_);
};

}  // namespace p4rt_app

#endif  // _SDN_CONTROLLER_MANAGER_H_

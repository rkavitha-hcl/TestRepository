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
#include "p4rt_app/p4runtime/sdn_controller_manager.h"

#include "absl/numeric/int128.h"
#include "absl/strings/str_join.h"
#include "glog/logging.h"

namespace p4rt_app {

using p4::v1::Uint128;

grpc::Status SdnControllerManager::HandleArbitrationUpdate(
    const p4::v1::MasterArbitrationUpdate& update,
    SdnControllerConnection* controller) {
  absl::MutexLock l(&lock_);

  // TODO: arbitration should fail with invalid device id.
  device_id_ = update.device_id();

  // Check for valid device id.
  if (update.device_id() != device_id_) {
    return grpc::Status(
        grpc::StatusCode::FAILED_PRECONDITION,
        absl::StrCat("This is the P4RT server for device ID ", device_id_,
                     " but not ", update.device_id(), "."));
  }

  // Checks valid role.
  if (update.role().id() != 0) {
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "Only the default role (with ID 0) is supported.");
  }

  absl::uint128 election_id_received = ToNativeUint128(update.election_id());

  if (!controller->IsInitialized()) {
    // If controller is newly connected.
    // Checks election_id received is not used by any other controllers with the
    // same device_id and role_id
    if (IsElectionIdInUse(election_id_received)) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Election ID is already used by another controller");
    }

    // Add the new controller to the list of all connected controllers.
    LOG(INFO) << "Adding new SDN controller with election_id: "
              << election_id_received;
    controllers_.push_back(controller);
  } else {
    // If the controller is already connected;
    absl::uint128 current_election_id = controller->election_id_.value();

    if (election_id_received == current_election_id) {
      // if election_id in the request matches the one assigned to the
      // current controller, no action needs to be performed regardless of
      // whether this controller is master or secondary since we only support
      // default role.
      LOG(INFO) << "Arbitration request is using the same election_id as the "
                << "current: " << election_id_received;
    } else {
      // Handles election_id change and potentially mastership change
      if (IsElectionIdInUse(election_id_received)) {
        return grpc::Status(
            grpc::StatusCode::INVALID_ARGUMENT,
            "Election ID is already used by another controller");
      }
    }
  }

  UpdateAndSendResponse(controller, election_id_received);
  return grpc::Status::OK;
}

void SdnControllerManager::HandleControllerDisconnect(
    SdnControllerConnection* controller) {
  absl::MutexLock l(&lock_);

  // Uninitialized controllers are not added into the list of connected
  // controllers yet and no need to handle disconnection.
  if (!controller->IsInitialized()) return;
  // controller cannot be null.
  bool is_master = IsMaster(controller);

  absl::uint128 election_id = controller->election_id_.value();
  for (auto it = controllers_.begin(); it != controllers_.end(); it++) {
    if ((*it)->election_id_.value() == election_id) {
      LOG(INFO) << "Dropping SDN controller with election_id: " << election_id;
      controllers_.erase(it);
      break;
    }
  }

  if (is_master) {
    BroadCastMasterChangeUpdate();
  }
}

bool SdnControllerManager::SendStreamMessageToMaster(
    const p4::v1::StreamMessageResponse& response) {
  absl::MutexLock l(&lock_);
  SdnControllerConnection* master = GetMasterController();
  if (master == nullptr) return false;
  master->SendStreamMessageResponse(response);
  return true;
}

// Based on P4Runtime spec, unspecified election_id (with high=0, low=0) is
// considered to be lower than any election_id, hence a controller with
// unspecified election_id can never become master. If highest_election_id_ is
// 0, then no master controller has came up yet.
bool SdnControllerManager::IsMasterElectionId(absl::uint128 election_id) {
  absl::MutexLock l(&lock_);
  if (highest_election_id_ != 0 && election_id == highest_election_id_) {
    // If the election_id matches the highest_election_id_, this loop verifies
    // that there is indeed a master controller with that election id connected
    // to the switch. This helps to avoid errors when a non-master controller
    // accidentally uses the eletion_id of a previously disconnected master when
    // sending Write requests.
    for (const auto& controller : controllers_) {
      if (controller->election_id_ == election_id) return true;
    }
  }
  return false;
}

void SdnControllerManager::UpdateAndSendResponse(
    SdnControllerConnection* controller, absl::uint128 new_election_id) {
  bool was_master = IsMaster(controller);
  controller->election_id_ = new_election_id;
  if (new_election_id != 0 && new_election_id >= highest_election_id_) {
    highest_election_id_ = new_election_id;
    BroadCastMasterChangeUpdate();
  } else {
    if (was_master) {
      BroadCastMasterChangeUpdate();
    } else {
      controller->SendStreamMessageResponse(
          PopulateMasterArbitrationResponse(/*is_master=*/false));
    }
  }
}

void SdnControllerManager::BroadCastMasterChangeUpdate() {
  for (const auto& controller : controllers_) {
    controller->SendStreamMessageResponse(
        PopulateMasterArbitrationResponse(IsMaster(controller)));
  }
}

p4::v1::StreamMessageResponse
SdnControllerManager::PopulateMasterArbitrationResponse(bool is_master) {
  p4::v1::StreamMessageResponse response;
  auto update = response.mutable_arbitration();
  update->set_device_id(device_id_);
  auto status = update->mutable_status();

  // Sets election_id to the highest_election_id if any master controller has
  // connected.
  if (highest_election_id_ > 0) {
    auto election_id = update->mutable_election_id();
    election_id->set_high(absl::Uint128High64(highest_election_id_));
    election_id->set_low(absl::Uint128Low64(highest_election_id_));
  }

  // Checks if there is a master controller.
  if (HasMasterController()) {
    if (is_master) {
      status->set_code(grpc::StatusCode::OK);
      status->set_message("This connection is a master connection.");
    } else {
      status->set_code(grpc::StatusCode::ALREADY_EXISTS);
      status->set_message(
          "This connection is a secondary connection, and there is another "
          "connection with a master.");
    }
    return response;
  }

  status->set_code(grpc::StatusCode::NOT_FOUND);
  status->set_message(
      "This connection is a secondary connection, and there is currently no "
      "master connection.");
  return response;
}

void SdnControllerConnection::SendStreamMessageResponse(
    const p4::v1::StreamMessageResponse& response) {
  if (!stream_->Write(response)) {
    LOG(ERROR) << "[arbitration]: failed to send MasterArbitrationUpdate: "
               << response.DebugString() << " grpc context " << context_
               << std::endl;
  }
}

bool SdnControllerManager::IsElectionIdInUse(absl::uint128 election_id) {
  for (const auto& controller : controllers_) {
    if (controller->election_id_.value() == election_id) return true;
  }
  return false;
}

bool SdnControllerManager::HasMasterController() {
  if (highest_election_id_ == 0) return false;
  for (const auto& controller : controllers_) {
    if (controller->election_id_.value() == highest_election_id_) {
      return true;
    }
  }
  return false;
}

SdnControllerConnection* SdnControllerManager::GetMasterController() const {
  if (highest_election_id_ == 0) return nullptr;
  for (const auto& controller : controllers_) {
    if (controller->election_id_.value() == highest_election_id_) {
      return controller;
    }
  }
  return nullptr;
}

absl::uint128 ToNativeUint128(const p4::v1::Uint128 id) {
  return absl::MakeUint128(id.high(), id.low());
}

}  // namespace p4rt_app

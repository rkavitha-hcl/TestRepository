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
#include "p4rt_app/event_monitoring/state_verification_events.h"

#include <thread>  // NOLINT

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "gutil/status.h"
#include "swss/consumernotifierinterface.h"
#include "swss/dbconnectorinterface.h"
#include "swss/rediscommand.h"

namespace p4rt_app {

constexpr absl::string_view kP4rtComponentName = "p4rt:p4rt";
constexpr absl::string_view kVerificationRespTable = "VERIFY_STATE_RESP_TABLE";

StateVerificationEvents::StateVerificationEvents(
    swss::ConsumerNotifierInterface& notification_channel,
    swss::DBConnectorInterface& response_channel)
    : notification_channel_(notification_channel),
      response_channel_(response_channel) {}

absl::Status StateVerificationEvents::WaitForEventAndVerifyP4Runtime() {
  constexpr absl::Duration timeout = absl::Hours(25);
  absl::MutexLock l(&event_lock_);

  std::string operation;
  std::string key;
  std::vector<swss::FieldValueTuple> field_values;
  if (!notification_channel_.WaitForNotificationAndPop(
          operation, key, field_values, absl::ToInt64Milliseconds(timeout))) {
    return gutil::UnknownErrorBuilder()
           << "State verification events failed/timed-out waiting for a "
           << "notification.";
  }

  // We only need to update state when asked about the P4RT App component.
  if (operation != kP4rtComponentName) {
    return absl::OkStatus();
  }

  // TODO: run P4RuntimeImpl state verification.
  std::string p4rt_status = "pass";

  // When updating AppStateDb we don't need to notify the caller. Simply update
  // the P4RT app entry with the current data.
  response_channel_.hmset(
      absl::StrCat(kVerificationRespTable, "|", kP4rtComponentName),
      {
          {"timestamp", key},
          {"status", p4rt_status},
          {"err_str", ""},
      });

  return absl::OkStatus();
}

void StateVerificationEvents::Start() {
  // There should only ever be one active thread.
  if (!event_thread_.joinable()) {
    event_thread_ = std::thread(
        &StateVerificationEvents::ContinuallyMonitorForEvents, this);
  }
}

void StateVerificationEvents::Stop() {
  stopping_.Notify();

  // Only join the thread if it has been started.
  if (event_thread_.joinable()) {
    event_thread_.join();
    LOG(INFO) << "Stop monitoring state verification events.";
  }
}

void StateVerificationEvents::ContinuallyMonitorForEvents() {
  LOG(INFO) << "Start monitoring state verification events.";
  while (!stopping_.HasBeenNotified()) {
    absl::Status status = WaitForEventAndVerifyP4Runtime();
    if (!status.ok()) {
      LOG(ERROR) << "Issue verifying P4RT App's state: " << status;
    }
  }
}

}  // namespace p4rt_app

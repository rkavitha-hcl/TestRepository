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
#include "p4rt_app/event_monitoring/port_change_events.h"

#include <deque>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "glog/logging.h"
#include "gutil/status.h"
#include "swss/rediscommand.h"
#include "swss/select.h"
#include "swss/selectable.h"
#include "swss/table.h"

namespace p4rt_app {

PortChangeEvents::PortChangeEvents(
    sonic::StateEventMonitor& state_event_monitor)
    : state_event_monitor_(state_event_monitor) {
  // Do nothing.
}

absl::Status PortChangeEvents::WaitForEventAndUpdateP4Runtime() {
  ASSIGN_OR_RETURN(std::deque<swss::KeyOpFieldsValuesTuple> events,
                   state_event_monitor_.GetNextEvents());

  for (const auto& event : events) {
    std::string op = kfvOp(event);
    std::string key = kfvKey(event);

    // Check for and "id" field in the event.
    std::string id;
    for (const auto& [field, value] : kfvFieldsValues(event)) {
      if (field == "id") id = value;
    }

    // If no id field is found we should try to remove it from the P4RT app
    // regardless of the Redis operation.
    if (id.empty()) {
      LOG(WARNING) << absl::StreamFormat(
          "Port Event: %s %s has no ID field. Removing from P4RT App.", op,
          key);
      continue;
    }

    LOG(INFO) << absl::StreamFormat("Port Event: %s %s with ID %s", op, key,
                                    id);
  }

  return absl::OkStatus();
}

}  // namespace p4rt_app

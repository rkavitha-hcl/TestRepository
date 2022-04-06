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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "glog/logging.h"
#include "gutil/status.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "swss/rediscommand.h"
#include "swss/select.h"
#include "swss/selectable.h"
#include "swss/table.h"

namespace p4rt_app {

PortChangeEvents::PortChangeEvents(
    P4RuntimeImpl& p4runtime, sonic::StateEventMonitor& state_event_monitor)
    : p4runtime_(p4runtime), state_event_monitor_(state_event_monitor) {
  // Do nothing.
}

absl::Status PortChangeEvents::WaitForEventAndUpdateP4Runtime() {
  ASSIGN_OR_RETURN(std::deque<swss::KeyOpFieldsValuesTuple> events,
                   state_event_monitor_.GetNextEvents());

  std::vector<std::string> failures = {"Port change event failures:"};
  for (const auto& event : events) {
    std::string op = kfvOp(event);
    std::string key = kfvKey(event);

    // Check for and "id" field in the event.
    std::string id;
    for (const auto& [field, value] : kfvFieldsValues(event)) {
      if (field == "id") id = value;
    }

    // We will try to apply all port events, and will not stop on a failure.
    absl::Status status;
    if (id.empty()) {
      // If no id field is found we should try to remove it from the P4RT app
      // regardless of the Redis operation.
      LOG(WARNING) << "'" << key << "' does not have an ID field.";
      status = p4runtime_.RemovePortTranslation(key);
    } else if (op == "SET") {
      status = p4runtime_.AddPortTranslation(key, id);
    } else if (op == "DEL") {
      status = p4runtime_.RemovePortTranslation(key);
    } else {
      LOG(ERROR) << "Unexpected operand '" << op << "'.";
      status = absl::InvalidArgumentError(
          absl::StrCat("unhandled SWSS operand '", op, "'"));
    }

    // Collect any status failures so we can report them up later.
    if (!status.ok()) {
      LOG(ERROR) << "Couldn't handle port event for '" << key
                 << "': " << status;
      failures.push_back(status.ToString());
    }
  }

  if (failures.size() > 1) {
    return gutil::UnknownErrorBuilder() << absl::StrJoin(failures, "\n  ");
  }
  return absl::OkStatus();
}

}  // namespace p4rt_app

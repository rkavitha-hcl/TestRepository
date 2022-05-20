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
#include "p4rt_app/event_monitoring/app_state_db_port_table_event.h"

#include <deque>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"

namespace p4rt_app {

AppStateDbPortTableEventHandler::AppStateDbPortTableEventHandler(
    P4RuntimeImpl& p4runtime)
    : p4runtime_(p4runtime) {
  // Do nothing.
}

absl::Status AppStateDbPortTableEventHandler::HandleEvent(
    const std::string& operation, const std::string& key,
    const std::vector<std::pair<std::string, std::string>>& values) {
  // Check the event for an ID field.
  std::string id;
  for (const auto& [field, value] : values) {
    if (field == "id") id = value;
  }

  // If it doesn't exist then we should try to remove it from the P4RT app.
  // Otherwise, apply the event's operation.
  if (id.empty()) {
    LOG(WARNING) << "'" << key << "' does not have an ID field.";
    return p4runtime_.RemovePortTranslation(key);
  } else if (operation == "SET") {
    return p4runtime_.AddPortTranslation(key, id);
  } else if (operation == "DEL") {
    return p4runtime_.RemovePortTranslation(key);
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unhandled SWSS operand '", operation, "'"));
}

}  // namespace p4rt_app

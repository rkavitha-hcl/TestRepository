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
#ifndef GOOGLE_P4RT_APP_EVENT_MONITORING_STATE_EVENT_MONITOR_H_
#define GOOGLE_P4RT_APP_EVENT_MONITORING_STATE_EVENT_MONITOR_H_

#include <deque>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "swss/dbconnector.h"
#include "swss/rediscommand.h"
#include "swss/subscriberstatetable.h"

namespace p4rt_app {
namespace sonic {

// The StateEventMonitor is used to subscribe to Redis events for specific SONiC
// tables.
class StateEventMonitor {
 public:
  StateEventMonitor(swss::DBConnector* db, const std::string& table_name);
  virtual ~StateEventMonitor() = default;

  // Not copyable or movable.
  StateEventMonitor(const StateEventMonitor&) = delete;
  StateEventMonitor& operator=(const StateEventMonitor&) = delete;

  // Blocks indefinitely until an event, or set of events occur on the table.
  virtual absl::StatusOr<std::deque<swss::KeyOpFieldsValuesTuple>>
  GetNextEvents();

  // Blocks until an event, or set of events occur on the table. If no event
  // occurs within the timeout period then a DEADLINE_EXCEEDED error will be
  // returned.
  virtual absl::StatusOr<std::deque<swss::KeyOpFieldsValuesTuple>>
  GetNextEventsWithTimeout(absl::Duration timeout);

 protected:
  // Default constructor should only be used to mock the class for testing.
  StateEventMonitor() = default;

 private:
  std::unique_ptr<swss::SubscriberStateTable> subscriber_state_table_;
};

}  // namespace sonic
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_EVENT_MONITORING_STATE_EVENT_MONITOR_H_

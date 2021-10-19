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
#ifndef GOOGLE_P4RT_APP_EVENT_MONITORING_PORT_CHANGE_EVENTS_H_
#define GOOGLE_P4RT_APP_EVENT_MONITORING_PORT_CHANGE_EVENTS_H_

#include "absl/status/status.h"
#include "p4rt_app/event_monitoring/state_event_monitor.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"

namespace p4rt_app {

// Monitors a table in the RedisDB for any state changes to the ports. When a
// change is noticed it will notify the P4RT App.
//
// Events that are monitored:
//   * Port addition/removal.
//   * Port ID field changes.
class PortChangeEvents {
 public:
  PortChangeEvents(P4RuntimeImpl& p4runtime_,
                   sonic::StateEventMonitor& state_event_monitor);

  absl::Status WaitForEventAndUpdateP4Runtime();

 private:
  P4RuntimeImpl& p4runtime_;
  sonic::StateEventMonitor& state_event_monitor_;
};

}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_EVENT_MONITORING_PORT_CHANGE_EVENTS_H_

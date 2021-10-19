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
#ifndef GOOGLE_P4RT_APP_EVENT_MONITORING_MOCK_STATE_EVENT_MONITOR_H_
#define GOOGLE_P4RT_APP_EVENT_MONITORING_MOCK_STATE_EVENT_MONITOR_H_

#include <deque>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "p4rt_app/event_monitoring/state_event_monitor.h"
#include "swss/rediscommand.h"

namespace p4rt_app {
namespace sonic {

class MockStateEventMonitor final : public StateEventMonitor {
 public:
  MOCK_METHOD(absl::StatusOr<std::deque<swss::KeyOpFieldsValuesTuple>>,
              GetNextEvents, (), (override));

  MOCK_METHOD(absl::StatusOr<std::deque<swss::KeyOpFieldsValuesTuple>>,
              GetNextEventsWithTimeout, (absl::Duration timeout), (override));
};

}  // namespace sonic
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_EVENT_MONITORING_MOCK_STATE_EVENT_MONITOR_H_

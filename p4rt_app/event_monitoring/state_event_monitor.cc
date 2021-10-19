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
#include "p4rt_app/event_monitoring/state_event_monitor.h"

#include <deque>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "gutil/status.h"
#include "swss/rediscommand.h"
#include "swss/select.h"
#include "swss/selectable.h"
#include "swss/subscriberstatetable.h"

namespace p4rt_app {
namespace sonic {
namespace {

absl::Status WaitForSubscribeEvent(std::optional<int> timeout_ms,
                                   swss::SubscriberStateTable* state_table) {
  swss::Select select;
  select.addSelectable(state_table);

  // If not timeout was set then wait indefinitely. Otherwise, we fail after the
  // timeout is reached.
  int error_code = swss::Select::OBJECT;
  swss::Selectable* selectable = nullptr;
  if (timeout_ms.has_value()) {
    error_code = select.select(&selectable, *timeout_ms);
  } else {
    error_code = select.select(&selectable);
  }

  // Translate swss::Select error code to absl::Status.
  switch (error_code) {
    case swss::Select::OBJECT:
      return absl::OkStatus();
    case swss::Select::ERROR:
      return gutil::UnknownErrorBuilder()
             << absl::StreamFormat("Waiting for event from '%s'.",
                                   state_table->getDbConnector()->getDbName());
    case swss::Select::TIMEOUT:
      return gutil::DeadlineExceededErrorBuilder()
             << absl::StreamFormat("Waiting for event from '%s'.",
                                   state_table->getDbConnector()->getDbName());
  }

  LOG(ERROR) << "Unhandled swss::Select enum value '" << error_code << "'.";
  return gutil::InternalErrorBuilder() << absl::StreamFormat(
             "Unexpected error code '%d' encountered while waiting for an "
             "event from '%s'.",
             error_code, state_table->getDbConnector()->getDbName());
}

}  // namespace

StateEventMonitor::StateEventMonitor(swss::DBConnector* db,
                                     const std::string& table_name)
    : subscriber_state_table_(
          std::make_unique<swss::SubscriberStateTable>(db, table_name)) {
  // do nothing.
}

absl::StatusOr<std::deque<swss::KeyOpFieldsValuesTuple>>
StateEventMonitor::GetNextEvents() {
  std::deque<swss::KeyOpFieldsValuesTuple> results;
  RETURN_IF_ERROR(
      WaitForSubscribeEvent(/*timeout_ms=*/{}, subscriber_state_table_.get()));
  subscriber_state_table_->pops(results);
  return results;
}

absl::StatusOr<std::deque<swss::KeyOpFieldsValuesTuple>>
StateEventMonitor::GetNextEventsWithTimeout(absl::Duration timeout) {
  std::deque<swss::KeyOpFieldsValuesTuple> results;
  RETURN_IF_ERROR(WaitForSubscribeEvent(absl::ToInt64Milliseconds(timeout),
                                        subscriber_state_table_.get()));
  subscriber_state_table_->pops(results);
  return results;
}

}  // namespace sonic
}  // namespace p4rt_app

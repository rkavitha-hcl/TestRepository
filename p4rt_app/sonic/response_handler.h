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
#ifndef GOOGLE_P4RT_APP_SONIC_RESPONSE_HANDLER_H_
#define GOOGLE_P4RT_APP_SONIC_RESPONSE_HANDLER_H_

#include "p4_pdpi/utils/ir.h"
#include "swss/consumernotifierinterface.h"
#include "swss/dbconnectorinterface.h"

namespace p4rt_app {
namespace sonic {

// Get and process response from the notification channel,
// if on error, restore the APPL_DB to the last good state.
// Uses, the key of the inserted entry to match the response
// and restore if needed.
// Input: keys - vector of keys that were used in the write request.
//        expected_response_count - number of expected responses from OrchAgent,
//        this can be less than the keys vector size because some write request
//        entries failed to be written to the APP_DB itself for some reason.
//        notification_interface - Notification channel on which the response is
//        expected.
//        app_db_client - redis handle to APP_DB.
//        state_db_client - redis handlet to APPL_STATE_DB.
// Output: ir_write_response - repeated protobuf of IrUpdateStatus, new
//         protobuf entries will be added if not allocated by the caller.
absl::Status GetAndProcessResponseNotification(
    absl::Span<const std::string> keys, int expected_response_count,
    swss::ConsumerNotifierInterface& notification_interface,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    pdpi::IrWriteResponse& ir_write_response);

}  // namespace sonic
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_SONIC_RESPONSE_HANDLER_H_

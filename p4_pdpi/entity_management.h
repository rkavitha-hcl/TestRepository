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

#ifndef GOOGLE_P4_PDPI_ENTITY_MANAGEMENT_H_
#define GOOGLE_P4_PDPI_ENTITY_MANAGEMENT_H_
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/ir.pb.h"

namespace pdpi {

// Create PI updates from PI table entries.
std::vector<p4::v1::Update> CreatePiUpdates(
    absl::Span<const p4::v1::TableEntry> pi_entries,
    p4::v1::Update_Type update_type);

// Sets the request's session parameters(e.g. device id). And sends a PI
// (program independent) read request.
absl::StatusOr<p4::v1::ReadResponse> SetIdAndSendPiReadRequest(
    P4RuntimeSession* session, p4::v1::ReadRequest& read_request);

// Sets the request's session parameters(e.g. device id and election id). And
// sends a PI (program independent) write request.
absl::Status SetIdsAndSendPiWriteRequest(P4RuntimeSession* session,
                                         p4::v1::WriteRequest& write_request);

// Sets the requests' session parameters(e.g. device id; election id). And sends
// PI (program independent) write requests.
absl::Status SetIdsAndSendPiWriteRequests(
    P4RuntimeSession* session,
    std::vector<p4::v1::WriteRequest>& write_requests);

// Reads PI (program independent) table entries.
absl::StatusOr<std::vector<p4::v1::TableEntry>> ReadPiTableEntries(
    P4RuntimeSession* session);

// Removes PI (program independent) table entries on the switch.
absl::Status RemovePiTableEntries(
    P4RuntimeSession* session, const IrP4Info& info,
    absl::Span<const p4::v1::TableEntry> pi_entries);

// Clears the table entries
absl::Status ClearTableEntries(P4RuntimeSession* session, const IrP4Info& info);

// Installs the given PI (program independent) table entry on the switch.
absl::Status InstallPiTableEntry(P4RuntimeSession* session,
                                 const p4::v1::TableEntry& pi_entry);

// Installs the given PI (program independent) table entries on the switch.
absl::Status InstallPiTableEntries(
    P4RuntimeSession* session, const IrP4Info& info,
    absl::Span<const p4::v1::TableEntry> pi_entries);

// Sends the given PI updates to the switch.
absl::Status SendPiUpdates(P4RuntimeSession* session,
                           absl::Span<const p4::v1::Update> pi_updates);

// Sets the forwarding pipeline from the given p4 info.
absl::Status SetForwardingPipelineConfig(P4RuntimeSession* session,
                                         const p4::config::v1::P4Info& p4info);

// Sets the forwarding pipeline from the given p4 info and device configuration.
absl::Status SetForwardingPipelineConfig(P4RuntimeSession* session,
                                         const p4::config::v1::P4Info& p4info,
                                         absl::string_view p4_device_config);

}  // namespace pdpi

#endif  // GOOGLE_P4_PDPI_ENTITY_MANAGEMENT_H_

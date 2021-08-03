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
#ifndef GOOGLE_P4RT_APP_SONIC_VRF_ENTRY_TRANSLATION_H_
#define GOOGLE_P4RT_APP_SONIC_VRF_ENTRY_TRANSLATION_H_

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "gutil/status.h"
#include "p4_pdpi/ir.h"
#include "swss/consumernotifierinterface.h"
#include "swss/dbconnectorinterface.h"
#include "swss/producerstatetableinterface.h"

namespace p4rt_app {
namespace sonic {

// Checks if the IR table entry uses a VRF ID, and if that ID already exists in
// the SONiC VRF_TABLE. If the entry does not have an ID we ignore it. If the
// entry as an ID, but it already exists in the SONiC VRF_TABLE we only update
// the reference counters. Otherwise, we create a new VRF_TABLE entry for the
// ID.
absl::Status InsertVrfEntryAndUpdateState(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>& reference_count);

// Checks if the IR table entry uses a VRF ID, and after updating the reference
// count if that ID is still needed. If the entry does not have an ID we ignore
// it. Otherwise, we update the reference count for the ID, and if nothing is
// still using it we remove the ID from the SONiC VRF_TABLE.
absl::Status DecrementVrfReferenceCount(
    swss::ProducerStateTableInterface& vrf_table,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>& reference_count);

// Checks if the existing AppDb entry, and the new IR TableEntry for a VRF ID.
// If neither have and IR or the ID is the same this method will be a no-op.
// Otherwise, it will remove the AppDb's ID value, and create the IR entries ID
// value.
absl::Status ModifyVrfEntryAndUpdateState(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    const std::unordered_map<std::string, std::string> app_db_values,
    const pdpi::IrTableEntry& ir_table_entry,
    absl::flat_hash_map<std::string, int>& reference_count);

// Walks over the reference count map and deletes the vrf entries whose
// reference count has reached zero.
absl::Status PruneVrfReferences(
    swss::ProducerStateTableInterface& vrf_table,
    swss::ConsumerNotifierInterface& vrf_notification,
    swss::DBConnectorInterface& app_db_client,
    swss::DBConnectorInterface& state_db_client,
    absl::flat_hash_map<std::string, int>& reference_count);

}  // namespace sonic
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_SONIC_VRF_ENTRY_TRANSLATION_H_

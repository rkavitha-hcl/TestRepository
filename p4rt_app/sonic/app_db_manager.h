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
#ifndef GOOGLE_P4RT_APP_SONIC_APP_DB_MANAGER_H_
#define GOOGLE_P4RT_APP_SONIC_APP_DB_MANAGER_H_

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "p4_pdpi/ir.pb.h"
#include "p4rt_app/sonic/redis_connections.h"

namespace p4rt_app {
namespace sonic {

// The P4RT App will usually target the AppDb P4RT table for which it is the
// only entry owner. However, in certain cases we can target other shared
// RedisDb tables.
enum class AppDbTableType {
  UNKNOWN,
  P4RT,
  VRF_TABLE,
};

// AppDb entries can be handled in any order by P4RT, but for error reporting
// purposes we need to keep track of the RPC update index.
struct AppDbEntry {
  int rpc_index;
  pdpi::IrTableEntry entry;
  p4::v1::Update::Type update_type;
  AppDbTableType appdb_table = AppDbTableType::UNKNOWN;
};

// List of all updates that should be made to the AppDb.
struct AppDbUpdates {
  std::vector<AppDbEntry> entries;
  int total_rpc_updates = 0;
};

// Takes a list of AppDb updates (i.e. inserts, modifies, or deletes) and
// translates them so that they are consumable by the AppDb. It will also
// create, or remove, any VRF IDs as needed.
absl::Status UpdateAppDb(P4rtTable& p4rt_table, VrfTable& vrf_table,
                         const AppDbUpdates& updates,
                         const pdpi::IrP4Info& p4_info,
                         pdpi::IrWriteResponse* response);

// Returns all P4RT keys currently installed in the AppStateDb. This does not
// include any keys that are currently being handled by the lower layers (i.e.
// keys starting with _).
std::vector<std::string> GetAllP4TableEntryKeys(P4rtTable& p4rt_table);

// The SONiC ProducerStateTables interface does not support reads so we must
// read entries at the AppStateDb scope. This means any ReadTable request key
// should include the "P4RT_" prefix assumed by this AppDbManager.
//
// Sample:
//   "P4RT:ROUTER_INTERFACE_TABLE:{\"router_interface_id\":\"16\"}"
//
// NOTE: The resulting IrTableEntry will not include the "P4RT:" prefix.
absl::StatusOr<pdpi::IrTableEntry> ReadP4TableEntry(
    P4rtTable& p4rt_table, const pdpi::IrP4Info& p4info,
    const std::string& key);

}  // namespace sonic
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_SONIC_APP_DB_MANAGER_H_

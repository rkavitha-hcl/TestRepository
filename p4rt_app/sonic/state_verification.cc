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
#include "p4rt_app/sonic/state_verification.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "gutil/status.h"

namespace p4rt_app {
namespace sonic {
namespace {

// Helper function to format RedisDb entries in error messages.
//
// Output would look like:
// {{"field","value"},{"other_field","other_value"}}
std::string PrettyPrintEntry(
    const std::unordered_map<std::string, std::string>& map) {
  int index = 0;
  std::vector<std::string> pairs(map.size());
  for (const auto& [key, value] : map) {
    pairs[index] = absl::StrCat(key, ",", value);
    ++index;
  }

  return absl::StrCat("{{", absl::StrJoin(pairs, "},{"), "}}");
}

std::string CompareAppDbAndAppStateDbEntries(
    absl::string_view key,
    const std::unordered_map<std::string, std::string>& app_db_entry,
    const std::unordered_map<std::string, std::string>& app_state_db_entry) {
  if (app_db_entry != app_state_db_entry) {
    return absl::StrFormat(
        "Entries for '%s' do not match: AppStateDb=%s AppDb=%s", key,
        PrettyPrintEntry(app_state_db_entry), PrettyPrintEntry(app_db_entry));
  }
  return "";
}

}  // namespace

std::vector<std::string> VerifyAppStateDbAndAppDbEntries(
    const std::string& table_name, swss::DBConnectorInterface& app_state_db,
    swss::DBConnectorInterface& app_db) {
  std::vector<std::string> failures;

  // Read all keys out of the AppDb and the AppStateDb.
  std::vector<std::string> app_db_keys =
      app_db.keys(absl::StrCat(table_name, ":*"));
  std::vector<std::string> app_state_db_keys =
      app_state_db.keys(absl::StrCat(table_name, ":*"));

  // Sort the keys so we can easily determine if one is missing.
  std::sort(app_db_keys.begin(), app_db_keys.end());
  std::sort(app_state_db_keys.begin(), app_state_db_keys.end());

  // Iterate through each vector in parallel comparing the entries for equality.
  auto app_db_iter = app_db_keys.begin();
  auto app_state_db_iter = app_state_db_keys.begin();
  while (app_db_iter != app_db_keys.end() &&
         app_state_db_iter != app_state_db_keys.end()) {
    if (*app_db_iter > *app_state_db_iter) {
      LOG(ERROR) << "AppDb is missing key: " << *app_db_iter;
      failures.push_back(
          absl::StrFormat("AppDb is missing key: %s", *app_db_iter));
      ++app_state_db_iter;
    } else if (*app_db_iter < *app_state_db_iter) {
      LOG(ERROR) << "AppStateDb is missing key: " << *app_state_db_iter;
      failures.push_back(
          absl::StrFormat("AppStateDb is missing key: %s", *app_state_db_iter));
      ++app_db_iter;
    } else {
      std::string error_message = CompareAppDbAndAppStateDbEntries(
          *app_db_iter, app_db.hgetall(*app_db_iter),
          app_state_db.hgetall(*app_state_db_iter));
      if (!error_message.empty()) {
        LOG(ERROR) << error_message;
        failures.push_back(error_message);
      }
      ++app_db_iter;
      ++app_state_db_iter;
    }
  }

  // Any extra keys in the AppDb must be missing from the AppStateDb.
  while (app_db_iter != app_db_keys.end()) {
    LOG(ERROR) << "AppStateDb is missing key: " << *app_db_iter;
    failures.push_back(
        absl::StrFormat("AppStateDb is missing key: %s", *app_db_iter));
    ++app_db_iter;
  }

  // Any extra keys in the AppStateDb must be missing from the AppDb.
  while (app_state_db_iter != app_state_db_keys.end()) {
    LOG(ERROR) << "AppDb is missing key: " << *app_state_db_iter;
    failures.push_back(
        absl::StrFormat("AppDb is missing key: %s", *app_state_db_iter));
    ++app_state_db_iter;
  }

  return failures;
}

}  // namespace sonic
}  // namespace p4rt_app

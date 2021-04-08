// Copyright (c) 2021, Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GOOGLE_LIB_GNMI_GNMI_HELPER_H_
#define GOOGLE_LIB_GNMI_GNMI_HELPER_H_

#include <string>
#include <type_traits>
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "p4_pdpi/connection_management.h"
#include "proto/gnmi/gnmi.grpc.pb.h"
#include "proto/gnmi/gnmi.pb.h"
#include "thinkit/switch.h"

namespace pins_test {

inline constexpr char kOpenconfigStr[] = "openconfig";
inline constexpr char kTarget[] = "target";

enum class GnmiSetType : char { kUpdate, kReplace, kDelete };

enum class OperStatus {
  kUnknown,
  kUp,
  kDown,
  kTesting,
};

// Builds gNMI Set Request for a given OC path, set type and set value.
// The path should be in the following format below.
// "interfaces/interface[Ethernet0]/config/mtu".
// The set value should be in the format ex: {\"mtu\":2000}.
absl::StatusOr<gnmi::SetRequest> BuildGnmiSetRequest(
    absl::string_view oc_path, GnmiSetType set_type,
    absl::string_view json_val = {});

// Builds gNMI Get Request for a given OC path.
// The path should be in the following format below.
// "interfaces/interface[Ethernet0]/config/mtu".
absl::StatusOr<gnmi::GetRequest> BuildGnmiGetRequest(
    absl::string_view oc_path, gnmi::GetRequest::DataType req_type);

// Parses Get Response to retrieve specific tag value.
absl::StatusOr<std::string> ParseGnmiGetResponse(
    const gnmi::GetResponse& response, absl::string_view match_tag);

absl::Status SetGnmiConfigPath(gnmi::gNMI::Stub* sut_gnmi_stub,
                               absl::string_view config_path,
                               GnmiSetType operation, absl::string_view value);

absl::StatusOr<std::string> GetGnmiStatePathInfo(
    gnmi::gNMI::StubInterface* sut_gnmi_stub, absl::string_view state_path,
    absl::string_view resp_parse_str);

template <class T>
std::string ConstructGnmiConfigSetString(std::string field, T value) {
  std::string result_str;
  if (std::is_integral<T>::value) {
    // result: "{\"field\":value}"
    result_str = absl::StrCat("{\"", field, "\":", value, "}");
  } else if (std::is_same<T, std::string>::value) {
    // result: "{\"field\":\"value\"};
    result_str = absl::StrCat("{\"", field, "\":\"", value, "\"}");
  }

  return result_str;
}

// Adding subtree to gNMI Subscription list.
void AddSubtreeToGnmiSubscription(absl::string_view subtree_root,
                                  gnmi::SubscriptionList& subscription_list,
                                  gnmi::SubscriptionMode mode,
                                  bool suppress_redundant,
                                  absl::Duration interval);

// Returns vector of elements in subscriber response.
absl::StatusOr<std::vector<absl::string_view>>
GnmiGetElementFromTelemetryResponse(const gnmi::SubscribeResponse& response);

absl::Status PushGnmiConfig(
    gnmi::gNMI::Stub& stub, const std::string& chassis_name,
    const std::string& gnmi_config,
    absl::uint128 election_id = pdpi::TimeBasedElectionId());

absl::Status PushGnmiConfig(thinkit::Switch& chassis,
                            const std::string& gnmi_config);

absl::Status CanGetAllInterfaceOverGnmi(
    gnmi::gNMI::Stub& stub, absl::Duration timeout = absl::Seconds(60));

absl::StatusOr<gnmi::GetResponse> GetAllInterfaceOverGnmi(
    gnmi::gNMI::Stub& stub, absl::Duration timeout = absl::Seconds(60));

// Checks if all interfaces are up.
absl::Status CheckAllInterfaceUpOverGnmi(
    gnmi::gNMI::Stub& stub, absl::Duration timeout = absl::Seconds(60));

// Returns gNMI Path for OC strings.
gnmi::Path ConvertOCStringToPath(absl::string_view oc_path);

// Gets the operational status of an interface.
absl::StatusOr<OperStatus> GetInterfaceOperStatusOverGnmi(
    gnmi::gNMI::Stub& stub, absl::string_view if_name);

// Parses the alarms JSON array returned from a gNMI Get request to
// "openconfig-system:system/alarms/alarm". Returns the list of alarms.
absl::StatusOr<std::vector<std::string>> ParseAlarms(
    const std::string& alarms_json);

// Gets alarms over gNMI.
absl::StatusOr<std::vector<std::string>> GetAlarms(
    gnmi::gNMI::StubInterface& gnmi_stub);

// Strips the beginning and ending double-quotes from the `string`.
absl::string_view StripQuotes(absl::string_view string);

}  // namespace pins_test
#endif  // GOOGLE_LIB_GNMI_GNMI_HELPER_H_

#ifndef GOOGLE_LIB_GNMI_GNMI_HELPER_H_
#define GOOGLE_LIB_GNMI_GNMI_HELPER_H_

#include <cstddef>
#include <string>
#include <type_traits>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "proto/gnmi/gnmi.grpc.pb.h"
#include "proto/gnmi/gnmi.pb.h"

namespace pins_test {

inline constexpr char kOpenconfigStr[] = "openconfig";

enum class GnmiSetType : char { kUpdate, kReplace, kDelete };

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
    gnmi::gNMI::Stub* sut_gnmi_stub, absl::string_view state_path,
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

}  // namespace pins_test
#endif  // GOOGLE_LIB_GNMI_GNMI_HELPER_H_

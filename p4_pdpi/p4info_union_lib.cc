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

#include "p4_pdpi/p4info_union_lib.h"

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/util/message_differencer.h"
#include "gutil/status.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/config/v1/p4types.pb.h"

namespace pdpi {
namespace {
// Checks if `infos` contains any field that is not supported by UnionP4Info,
// such as Extern.
absl::Status ContainsUnsupportedField(
    const std::vector<p4::config::v1::P4Info>& infos) {
  for (const auto& info : infos) {
    if (!info.externs().empty()) {
      return absl::UnimplementedError(
          "UnionP4Info can not union Extern field.");
    }
  }
  return absl::OkStatus();
}

// Unions pkg_info field of`info` into `unioned_info`.
// If pkg_info of `info` differs from other pkg_info, return
// InvalidArgumentError.
absl::Status UnionPkgInfos(const p4::config::v1::P4Info& info,
                           p4::config::v1::P4Info& unioned_info) {
  if (!unioned_info.has_pkg_info()) {
    *unioned_info.mutable_pkg_info() = info.pkg_info();
    return absl::OkStatus();
  }
  google::protobuf::util::MessageDifferencer msg_diff;
  std::string diff_result;
  msg_diff.ReportDifferencesToString(&diff_result);
  if (!msg_diff.Compare(info.pkg_info(), unioned_info.pkg_info())) {
    return absl::InvalidArgumentError(absl::StrCat(
        "pkg_info is not the same for all infos. Diff result: ", diff_result));
  }

  return absl::OkStatus();
}

// Unions `repeated_fields` of type T into `unioned_repeated_fields` using
// Preamble::id. This function assumes type T has Preamble::id field.
template <class T>
absl::Status UnionRepeatedFields(
    const google::protobuf::RepeatedPtrField<T>& repeated_fields,
    google::protobuf::RepeatedPtrField<T>& unioned_repeated_fields) {
  for (const auto& field : repeated_fields) {
    // If a field matching the given field's ID is already in
    // `unioned_repeated_fields`, checks that the fields are equal, returning an
    // error with the diff if they're not.
    uint32_t preamble_id = field.preamble().id();
    bool found_field_with_same_preamble_id = false;
    for (const auto& field_in_union : unioned_repeated_fields) {
      if (field_in_union.preamble().id() == preamble_id) {
        found_field_with_same_preamble_id = true;
        google::protobuf::util::MessageDifferencer msg_diff;
        std::string diff_result;
        msg_diff.ReportDifferencesToString(&diff_result);
        if (!msg_diff.Compare(field, field_in_union)) {
          return absl::InvalidArgumentError(absl::Substitute(
              "Fields of type $0, which share the same preamble id, $1, "
              "are not identical. Diff result: $2",
              field_in_union.GetDescriptor()->name(), preamble_id,
              diff_result));
        }
        break;
      }
    }
    if (!found_field_with_same_preamble_id) {
      *unioned_repeated_fields.Add() = field;
    }
  }
  return absl::OkStatus();
}

// Unions all type_info in `info` into `unioned_info` using the key of
// P4TypeInfo::new_types. Return UnimplementedError if fields other than
// new_types is in type_info of any of `info`
absl::Status UnionTypeInfo(const p4::config::v1::P4Info& info,
                           p4::config::v1::P4Info& unioned_info) {
  if (info.type_info().structs_size() != 0 ||
      info.type_info().headers_size() != 0 ||
      info.type_info().header_unions_size() != 0 ||
      info.type_info().enums_size() != 0 || info.type_info().has_error() ||
      info.type_info().serializable_enums_size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "UnionTypeInfo only support P4TypeInfo::new_types. P4TypeInfo: ",
        info.type_info().DebugString()));
  }
  if (!unioned_info.has_type_info()) {
    *unioned_info.mutable_type_info() = info.type_info();
    return absl::OkStatus();
  }
  for (const auto& [type_name, type_spec] : info.type_info().new_types()) {
    auto it = unioned_info.type_info().new_types().find(type_name);
    if (it != unioned_info.type_info().new_types().end()) {
      google::protobuf::util::MessageDifferencer msg_diff;
      std::string diff_result;
      msg_diff.ReportDifferencesToString(&diff_result);
      if (!msg_diff.Compare(type_spec, it->second)) {
        return absl::InvalidArgumentError(
            absl::Substitute("P4NewTypeSpec that share the same key, $0, are "
                             "not identical. Diff result: $1",
                             it->first, diff_result));
      } else {
        (*unioned_info.mutable_type_info()->mutable_new_types())[type_name] =
            type_spec;
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<p4::config::v1::P4Info> UnionP4info(
    const std::vector<p4::config::v1::P4Info>& infos) {
  RETURN_IF_ERROR(ContainsUnsupportedField(infos));
  p4::config::v1::P4Info unioned_info;
  for (const auto& info : infos) {
    RETURN_IF_ERROR(UnionPkgInfos(info, unioned_info));
    RETURN_IF_ERROR(
        UnionRepeatedFields(info.tables(), *unioned_info.mutable_tables()));
    RETURN_IF_ERROR(
        UnionRepeatedFields(info.actions(), *unioned_info.mutable_actions()));
    RETURN_IF_ERROR(UnionRepeatedFields(
        info.action_profiles(), *unioned_info.mutable_action_profiles()));
    RETURN_IF_ERROR(
        UnionRepeatedFields(info.counters(), *unioned_info.mutable_counters()));
    RETURN_IF_ERROR(UnionRepeatedFields(
        info.direct_counters(), *unioned_info.mutable_direct_counters()));
    RETURN_IF_ERROR(
        UnionRepeatedFields(info.meters(), *unioned_info.mutable_meters()));
    RETURN_IF_ERROR(UnionRepeatedFields(info.direct_meters(),
                                        *unioned_info.mutable_direct_meters()));
    RETURN_IF_ERROR(UnionRepeatedFields(
        info.controller_packet_metadata(),
        *unioned_info.mutable_controller_packet_metadata()));
    RETURN_IF_ERROR(UnionRepeatedFields(info.value_sets(),
                                        *unioned_info.mutable_value_sets()));
    RETURN_IF_ERROR(UnionRepeatedFields(info.registers(),
                                        *unioned_info.mutable_registers()));
    RETURN_IF_ERROR(
        UnionRepeatedFields(info.digests(), *unioned_info.mutable_digests()));
    RETURN_IF_ERROR(UnionTypeInfo(info, unioned_info));
  }

  return unioned_info;
}
}  // namespace pdpi

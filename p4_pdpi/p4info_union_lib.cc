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

// Returns the id of the given `p4info.proto` Message (e.g. `Table`, `Action`,
// etc.) The generic implementation expects the message to contain a preamble,
// which contains an id. Messages without preambles have specialized
// implementations given below.
template <typename T>
uint32_t GetId(const T& field) {
  return field.preamble().id();
}
uint32_t GetId(const p4::config::v1::MatchField& field) { return field.id(); }
uint32_t GetId(const p4::config::v1::ActionRef& field) { return field.id(); }
uint32_t GetId(const p4::config::v1::Preamble& field) { return field.id(); }
uint32_t GetId(
    const p4::config::v1::ControllerPacketMetadata::Metadata& field) {
  return field.id();
}
uint32_t GetId(const p4::config::v1::Action::Param& field) {
  return field.id();
}

// Checks that two fields are equal returning an InvalidArgumentError containing
// the diff otherwise.
template <typename T>
absl::Status AssertEqual(const T& field1, const T& field2) {
  std::string diff_result;
  google::protobuf::util::MessageDifferencer msg_diff;
  msg_diff.ReportDifferencesToString(&diff_result);
  if (!msg_diff.Compare(field1, field2)) {
    return absl::InvalidArgumentError(
        absl::Substitute("diff result from comparing fields of type '$0': $1",
                         field1.GetDescriptor()->name(), diff_result));
  }
  return absl::OkStatus();
}

// Unions the given two instances of a field, asserting also that their IDs are
// equal. Returns invalid argument error if unioning fails, and internal error
// if the IDs are not equal, since the latter is always a serious programming
// flaw. The default implementation here only allows fields to be exactly equal,
// doing no additional unioning; the function may be specialized for concrete
// fields to implement a more sophisticated unioning logic.
// Requires: GetId(field) == GetId(unioned_field)
template <typename T>
absl::Status UnionFieldAssertingIdenticalId(const T& field,
                                            const T& unioned_field) {
  if (GetId(field) != GetId(unioned_field)) {
    // We throw an internal error here, rather than an InvalidArgumentError, to
    // signal that this is a catastrophic failure, which should be unreachable
    // code. The function has been used incorrectly in a way suggesting that the
    // library is wrong, rather than the p4infos given to its entry function.
    return absl::InternalError(absl::Substitute(
        "$0 tried to union fields with different ids: $1 and $2", __func__,
        GetId(field), GetId(unioned_field)));
  }

  // We fail unless the fields are identical.
  RETURN_IF_ERROR(AssertEqual(field, unioned_field)).SetPrepend()
      << absl::Substitute(
             "$0 failed since fields sharing the same id, '$1', were not "
             "equal: ",
             __func__, GetId(field));
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
  RETURN_IF_ERROR(AssertEqual(info.pkg_info(), unioned_info.pkg_info()))
          .SetPrepend()
      << absl::StrCat(__func__, " failed: ");

  return absl::OkStatus();
}

// Unions `fields` of type T into `unioned_fields` using their ids (as returned
// by GetId).
template <class T>
absl::Status UnionRepeatedFields(
    const google::protobuf::RepeatedPtrField<T>& fields,
    google::protobuf::RepeatedPtrField<T>& unioned_fields) {
  for (const auto& field : fields) {
    // If a field matching the given field's ID is already in
    // `unioned_fields`, checks that the fields are equal, returning
    // an error with the diff if they're not.
    uint32_t id = GetId(field);
    bool found_field_with_same_id = false;
    for (auto& field_in_union : unioned_fields) {
      if (GetId(field_in_union) == id) {
        found_field_with_same_id = true;
        RETURN_IF_ERROR(UnionFieldAssertingIdenticalId(field, field_in_union));
        break;
      }
    }
    if (!found_field_with_same_id) {
      *unioned_fields.Add() = field;
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
      RETURN_IF_ERROR(AssertEqual(type_spec, it->second)).SetPrepend()
          << absl::Substitute(
                 "$0 failed since fields sharing the same key '$1', were not "
                 "equal: ",
                 __func__, it->first);
      (*unioned_info.mutable_type_info()->mutable_new_types())[type_name] =
          type_spec;
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

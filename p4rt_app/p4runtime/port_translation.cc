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
#include "p4rt_app/p4runtime/port_translation.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "glog/logging.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "p4_pdpi/ir.h"

namespace p4rt_app {
namespace {

// TODO: We should be relying on the type, not the field name.
bool IsPortName(absl::string_view name) {
  return (name == "port") || (name == "watch_port") || (name == "in_port") ||
         (name == "out_port") || (name == "dst_port");
}

absl::Status TranslatePortValue(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    pdpi::IrValue& value) {
  switch (value.format_case()) {
    case pdpi::IrValue::kStr: {
      ASSIGN_OR_RETURN(*value.mutable_str(),
                       TranslatePort(direction, port_map, value.str()));
      break;
    }
    default:
      return gutil::InvalidArgumentErrorBuilder()
             << "Port value must use Format::STRING, but found "
             << value.format_case() << " instead.";
  }

  return absl::OkStatus();
}

absl::Status TranslatePortsInAction(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    pdpi::IrActionInvocation& action) {
  for (auto& param : *action.mutable_params()) {
    if (IsPortName(param.name())) {
      RETURN_IF_ERROR(
          TranslatePortValue(direction, port_map, *param.mutable_value()))
          << " For action paramter " << param.name() << ".";
    }
  }
  return absl::OkStatus();
}

absl::Status TranslatePortsInActionSet(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    pdpi::IrActionSet& action_set) {
  for (auto& action : *action_set.mutable_actions()) {
    RETURN_IF_ERROR(
        TranslatePortsInAction(direction, port_map, *action.mutable_action()));

    if (!action.watch_port().empty()) {
      ASSIGN_OR_RETURN(*action.mutable_watch_port(),
                       TranslatePort(direction, port_map, action.watch_port()));
    }
  }
  return absl::OkStatus();
}

absl::Status TranslatePortsInMatchFields(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    pdpi::IrMatch& match) {
  // If the match field name isn't for a port then ignore it.
  if (!IsPortName(match.name())) return absl::OkStatus();

  // Otherwise, we expect the port field to be an exact match or optional
  // field.
  switch (match.match_value_case()) {
    case pdpi::IrMatch::kExact:
      RETURN_IF_ERROR(
          TranslatePortValue(direction, port_map, *match.mutable_exact()))
          << " For match field " << match.name() << ".";
      break;
    case pdpi::IrMatch::kOptional:
      RETURN_IF_ERROR(TranslatePortValue(
          direction, port_map, *match.mutable_optional()->mutable_value()))
          << " For match field " << match.name() << ".";
      break;
    default:
      return gutil::InvalidArgumentErrorBuilder()
             << "The port match field is not an exact or optional match "
             << "type: " << match.name();
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::string> TranslatePort(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    const std::string& port_key) {
  switch (direction) {
    case PortTranslationDirection::kForController: {
      auto value = port_map.left.find(port_key);
      if (value == port_map.left.end()) {
        return gutil::InvalidArgumentErrorBuilder()
               << "Cannot translate port '" << absl::CHexEscape(port_key)
               << "' for controller. Does it exist and has it been configured "
               << "with an ID?";
      }
      return value->second;
    }
    case PortTranslationDirection::kForOrchAgent: {
      auto value = port_map.right.find(port_key);
      if (value == port_map.right.end()) {
        return gutil::InvalidArgumentErrorBuilder()
               << "Cannot translate port '" << absl::CHexEscape(port_key)
               << "' for OrchAgent. Does it exist and has it been configured "
               << "with an ID?";
      }
      return value->second;
    }
  }
  return gutil::InternalErrorBuilder() << "Could not translate port because "
                                          "unsupported direction was selected.";
}

absl::Status TranslatePortIdAndNames(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    pdpi::IrTableEntry& entry) {
  // Handle match fields..
  for (auto& match : *entry.mutable_matches()) {
    RETURN_IF_ERROR(TranslatePortsInMatchFields(direction, port_map, match));
  }

  // Handle both a single action, and a action set.
  if (entry.has_action()) {
    RETURN_IF_ERROR(
        TranslatePortsInAction(direction, port_map, *entry.mutable_action()));
  } else if (entry.has_action_set()) {
    RETURN_IF_ERROR(TranslatePortsInActionSet(direction, port_map,
                                              *entry.mutable_action_set()));
  }

  return absl::OkStatus();
}

}  // namespace p4rt_app

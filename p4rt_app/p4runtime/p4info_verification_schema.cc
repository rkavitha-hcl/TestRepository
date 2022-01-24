// Copyright 2022 Google LLC
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
#include "p4rt_app/p4runtime/p4info_verification_schema.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gutil/status.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.pb.h"
#include "p4rt_app/p4runtime/p4info_verification_schema.pb.h"
#include "p4rt_app/utils/table_utility.h"

namespace p4rt_app {
namespace {
// Return the Schema match type for the provided P4 match type or return an
// error if the match type is not supported.
absl::StatusOr<MatchSchema::MatchType> ToSchemaMatchType(
    p4::config::v1::MatchField::MatchType p4_match_type) {
  switch (p4_match_type) {
    case p4::config::v1::MatchField::EXACT:
    case p4::config::v1::MatchField::OPTIONAL:
      return MatchSchema::EXACT;
    case p4::config::v1::MatchField::LPM:
      return MatchSchema::LPM;
    case p4::config::v1::MatchField::TERNARY:
      return MatchSchema::TERNARY;
    case p4::config::v1::MatchField::RANGE:
    case p4::config::v1::MatchField::UNSPECIFIED:
    default:
      return gutil::InvalidArgumentErrorBuilder()
             << "Match type ("
             << p4::config::v1::MatchField::MatchType_Name(p4_match_type)
             << ") is unsupported.";
  }
}

// Build the MatchSchema from the IR match field or return an error if the match
// field cannot be translated to a supported schema.
absl::StatusOr<MatchSchema> ToMatchSchema(
    std::string name, const pdpi::IrMatchFieldDefinition& match_field) {
  MatchSchema match_schema;
  match_schema.set_name(name);

  match_schema.set_format(match_field.format());
  if (match_field.format() == pdpi::Format::HEX_STRING) {
    match_schema.set_bitwidth(match_field.match_field().bitwidth());
    if (match_schema.bitwidth() <= 0) {
      return gutil::InvalidArgumentErrorBuilder()
             << "HEX_STRING match fields must have a bitwidth.";
    }
  }

  // TODO Fill in: is_port

  ASSIGN_OR_RETURN(MatchSchema::MatchType match_type,
                   ToSchemaMatchType(match_field.match_field().match_type()));
  match_schema.set_type(match_type);
  return match_schema;
}

// Build the ActionSchema from the IR action or return an error if the action
// cannot be translated to a supported schema.
absl::StatusOr<ActionSchema> ToActionSchema(
    const pdpi::IrActionDefinition& action) {
  ActionSchema action_schema;
  action_schema.set_name(action.preamble().alias());
  for (const auto& [param_name, param] : action.params_by_name()) {
    auto& param_schema = *action_schema.add_parameters();
    param_schema.set_name(param_name);
    param_schema.set_format(param.format());
    if (param_schema.format() == pdpi::Format::HEX_STRING) {
      param_schema.set_bitwidth(param.param().bitwidth());
      if (param_schema.bitwidth() <= 0) {
        return gutil::InvalidArgumentErrorBuilder()
               << "HEX_STRING parameters must have a bitwidth.";
      }
    }
    // TODO Fill in: is_port
  }

  return action_schema;
}

// Build the TableSchema from the IR table or return an error if the table
// cannot be translated to a supported schema.
absl::StatusOr<FixedTableSchema> ToTableSchema(
    const std::string& table_name, const pdpi::IrTableDefinition& table) {
  if (table.has_counter()) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Fixed tables may not contain counters.";
  }
  if (table.has_meter()) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Fixed tables may not contain meters.";
  }

  FixedTableSchema table_schema;
  table_schema.set_name(table_name);
  if (table.match_fields_by_name().empty()) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Table must contain at least one match field.";
  }
  for (const auto& [name, match_field] : table.match_fields_by_name()) {
    ASSIGN_OR_RETURN(*table_schema.add_match_fields(),
                     ToMatchSchema(name, match_field),
                     _.SetPrepend() << "match_field '" << name << "': ");
  }
  for (const auto& action_reference : table.entry_actions()) {
    ASSIGN_OR_RETURN(
        *table_schema.add_actions(), ToActionSchema(action_reference.action()),
        _.SetPrepend() << "action '"
                       << action_reference.action().preamble().alias()
                       << "': ");
  }
  return table_schema;
}
}  // namespace

// Build the Schema from the IrP4Info or return an error if the IrP4Info cannot
// be translated to a valid schema.
absl::StatusOr<P4InfoVerificationSchema> ConvertToSchema(
    const pdpi::IrP4Info& ir_p4info) {
  P4InfoVerificationSchema schema;

  for (const auto& [table_name, table] : ir_p4info.tables_by_name()) {
    ASSIGN_OR_RETURN(table::Type table_type, GetTableType(table),
                     _.SetPrepend() << "[P4RT App] Failed to verify table '"
                                    << table_name << "': ");
    if (table_type != table::Type::kFixed) continue;

    ASSIGN_OR_RETURN(*schema.add_tables(), ToTableSchema(table_name, table),
                     _.SetPrepend() << "[P4RT App] Failed to verify table '"
                                    << table_name << "': ");
  }

  return schema;
}

}  // namespace p4rt_app

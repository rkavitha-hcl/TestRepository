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

#include "p4rt_app/p4runtime/p4runtime_tweaks.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "p4_pdpi/utils/annotation_parser.h"

namespace p4rt_app {
namespace {

bool IsPortName(absl::string_view name) {
  return (name == "port") || (name == "watch_port") || (name == "in_port") ||
         (name == "out_port") || (name == "dst_port");
}

void SetPortMatchFieldFormatToString(pdpi::IrMatchFieldDefinition& match_def,
                                     bool log) {
  if (!IsPortName(match_def.match_field().name())) return;
  if (log) {
    LOG(WARNING) << "Updating match field '" << match_def.match_field().name()
                 << "' format to STRING.";
  }
  match_def.set_format(pdpi::Format::STRING);
}

void SetCompositeUdfFieldFormatToHexString(
    pdpi::IrMatchFieldDefinition& match_def) {
  static constexpr absl::string_view kCompositeMatchLabel = "composite_field";
  static constexpr absl::string_view kUdfMatchLabel = "sai_udf";

  if (match_def.format() == pdpi::Format::HEX_STRING) return;

  auto parse_result = pdpi::GetAnnotationAsArgList(
      kCompositeMatchLabel, match_def.match_field().annotations());
  if (!parse_result.ok()) return;     // Composite annotation not found.
  if (parse_result->empty()) return;  // No sub-fields.

  // Check if all sub-fields are UDF.
  for (const pdpi::annotation::AnnotationComponents& annotation :
       pdpi::GetAllAnnotations(*parse_result)) {
    if (annotation.label != kUdfMatchLabel) {
      return;
    }
  }

  match_def.set_format(pdpi::Format::HEX_STRING);
}

void TweakForOrchAgent(pdpi::IrTableDefinition& table_def) {
  for (auto& [match_id, match_def] : *table_def.mutable_match_fields_by_id()) {
    SetPortMatchFieldFormatToString(match_def, /*log=*/true);
    SetCompositeUdfFieldFormatToHexString(match_def);
  }
  for (auto& [match_name, match_def] :
       *table_def.mutable_match_fields_by_name()) {
    SetPortMatchFieldFormatToString(match_def, /*log=*/false);
    SetCompositeUdfFieldFormatToHexString(match_def);
  }
}

}  // namespace

void P4RuntimeTweaks::ForOrchAgent(pdpi::IrP4Info& p4_info) {
  for (auto& [table_id, table_def] : *p4_info.mutable_tables_by_id()) {
    TweakForOrchAgent(table_def);
  }
  for (auto& [table_id, table_def] : *p4_info.mutable_tables_by_name()) {
    TweakForOrchAgent(table_def);
  }
}

}  // namespace p4rt_app

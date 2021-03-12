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
#ifndef P4RUNTIME_PORT_TRANSLATION_H_
#define P4RUNTIME_PORT_TRANSLATION_H_

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "boost/bimap.hpp"
#include "p4_pdpi/ir.h"

namespace p4rt_app {

enum class PortTranslationDirection { kForController, kForOrchAgent };

// Translates a single port field.
absl::StatusOr<std::string> TranslatePort(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    const std::string& port_key);

// Translates all the port field in a PDPI IrTableEntry based on a given port
// mapping. The library assumes all port names are encoded as strings. If not
// it will return an InvalidArgument error.
//
// NOTE: when translating data for the OrchAgent to consume the mapping should
//       be ids_to_names, and when translating for the controller the mapping
//       should be names_to_ids.
absl::Status TranslatePortIdAndNames(
    PortTranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    pdpi::IrTableEntry& entry);

}  // namespace p4rt_app

#endif  // P4RUNTIME_PORT_TRANSLATION_H_

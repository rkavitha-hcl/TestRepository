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
#ifndef GOOGLE_P4RT_APP_P4RUNTIME_IR_TRANSLATION_H_
#define GOOGLE_P4RT_APP_P4RUNTIME_IR_TRANSLATION_H_

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "boost/bimap.hpp"
#include "p4_pdpi/ir.h"

namespace p4rt_app {

enum class TranslationDirection { kForController, kForOrchAgent };

struct TranslateTableEntryOptions {
  const TranslationDirection& direction;
  const pdpi::IrP4Info& ir_p4_info;
  bool translate_port_ids = false;

  // boost::bimap<SONiC port name, controller ID>;
  const boost::bimap<std::string, std::string>& port_map;
};

// Translates only a port string value.
absl::StatusOr<std::string> TranslatePort(
    TranslationDirection direction,
    const boost::bimap<std::string, std::string>& port_map,
    const std::string& port_key);

// Translates all the port fields, and VRF ID in a PDPI IrTableEntry. The
// library assumes all port names, and VRF IDs are encodded as strings. If not
// it will return an InvalidArgument error.
absl::Status TranslateTableEntry(const TranslateTableEntryOptions& options,
                                 pdpi::IrTableEntry& entry);

}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_P4RUNTIME_IR_TRANSLATION_H_

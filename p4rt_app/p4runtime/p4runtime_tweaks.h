/*
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _P4RUNTIME_P4RUNTIME_TWEAKS_H_
#define _P4RUNTIME_P4RUNTIME_TWEAKS_H_

#include "absl/container/flat_hash_map.h"
#include "p4_pdpi/ir.h"

namespace p4rt_app {

// Currently the controller does not format all P4RT requests as expected for
// SONiC. The P4RuntimeTweaks API fakes these translations to speed up
// development.
//
// NOTE: THIS CLASS SHOULD BE REMOVED BEFORE FINAL RELEASE
class P4RuntimeTweaks {
 public:
  static void ForOrchAgent(pdpi::IrP4Info& p4_info);
};

}  // namespace p4rt_app

#endif  // _P4RUNTIME_P4RUNTIME_TWEAKS_H_

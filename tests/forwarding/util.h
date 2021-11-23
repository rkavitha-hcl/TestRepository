// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GOOGLE_TESTS_FORWARDING_UTIL_H_
#define GOOGLE_TESTS_FORWARDING_UTIL_H_

#include <functional>
#include <string>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"

namespace gpins {

// Calls given callback up to the given number of times with the given delay in
// between successive attempts, returning ok status as soon as the callback
// succeeds or the callback's final error status otherwise.
absl::Status TryUpToNTimes(int n, absl::Duration delay,
                           std::function<absl::Status(int iteration)> callback);
absl::Status TryUpToNTimes(int n, absl::Duration delay,
                           std::function<absl::Status()> callback);

// Injects the given test packet via packetIO at the egress port specified by
// the test packet, using the given P4RT session.
absl::Status InjectEgressPacket(const std::string& port,
                                const std::string& packet,
                                const pdpi::IrP4Info& p4info,
                                pdpi::P4RuntimeSession* p4rt);

// Inject the given packet into the ingress pipeline of the switch.
absl::Status InjectIngressPacket(const std::string& packet,
                                 const pdpi::IrP4Info& p4info,
                                 pdpi::P4RuntimeSession* p4rt);

}  // namespace gpins

#endif  // GOOGLE_TESTS_FORWARDING_UTIL_H_

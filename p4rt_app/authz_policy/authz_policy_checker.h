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

#ifndef AUTHZ_POLICY_AUTHZ_POLICY_CHECKER_H_
#define AUTHZ_POLICY_AUTHZ_POLICY_CHECKER_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "gutil/status.h"
#include "p4rt_app/proto/authorization_policy.pb.h"

namespace p4rt_app {
namespace grpc_authz_processor {

// GrpcAuthzPolicyChecker parses the authz policy configuration proto and
// performs authorization checks.
class GrpcAuthzPolicyChecker {
 public:
  explicit GrpcAuthzPolicyChecker(
      const google::p4rt::AuthorizationPolicy& authz_policy);
  ~GrpcAuthzPolicyChecker() = default;

  // Performs the authorization check for the given RPC and username.
  absl::Status Check(const std::string& service, const std::string& rpc,
                     const std::string& username) const;

 private:
  // The ServicePolicy struct contains the policy for each RPC and the default
  // policy.
  struct ServicePolicy {
    // Set of authorized users for each RPC.
    // Using RPC method names as the keys.
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>
        rpc_policies;

    // The default set of authorized users for RPCs that are not in
    // rpc_policies.
    absl::flat_hash_set<std::string> default_policy;
  };

  absl::flat_hash_map<std::string, ServicePolicy>
      service_policies_;  // Using RPC service names as the keys.
};

}  // namespace grpc_authz_processor
}  // namespace p4rt_app

#endif  // AUTHZ_POLICY_AUTHZ_POLICY_CHECKER_H_

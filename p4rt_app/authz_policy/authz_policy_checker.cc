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

#include "p4rt_app/authz_policy/authz_policy_checker.h"

#include "glog/logging.h"

namespace p4rt_app {
namespace grpc_authz_processor {
namespace {

// Returns the set of authorized users for an RpcPolicy.
// Authorized users are determined by the union of all policies matching the
// RpcPolicy labels.
absl::flat_hash_set<std::string> GetUserSet(
    const google::p4rt::AuthorizationPolicy::RpcPolicy& proto,
    const absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>&
        label_policies) {
  absl::flat_hash_set<std::string> policies;
  for (const auto& label : proto.labels()) {
    if (label_policies.find(label) == label_policies.end()) {
      LOG(WARNING) << "Authorization policy label \"" << label
                   << "\" cannot be found.";
      continue;
    }
    for (const auto& user : label_policies.at(label)) {
      policies.insert(user);
    }
  }
  return policies;
}

}  // namespace

GrpcAuthzPolicyChecker::GrpcAuthzPolicyChecker(
    const google::p4rt::AuthorizationPolicy& authz_policy) {
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>
      label_policies;
  for (const auto& policy : authz_policy.labels_to_principals()) {
    const std::string& label = policy.first;
    const google::p4rt::AuthorizationPolicy::AuthorizedPrincipals& principals =
        policy.second;
    for (const auto& user : principals.mdb_users()) {
      label_policies[label].insert(user);
    }
  }

  for (const auto& service_policy : authz_policy.service_policies()) {
    const std::string& service = service_policy.first;
    const google::p4rt::AuthorizationPolicy::ServicePolicy& rpc_policies =
        service_policy.second;
    for (const auto& rpc_policy : rpc_policies.rpc_policies()) {
      const std::string& rpc = rpc_policy.first;
      const google::p4rt::AuthorizationPolicy::RpcPolicy& policy =
          rpc_policy.second;
      service_policies_[service].rpc_policies[rpc] =
          GetUserSet(policy, label_policies);
    }
    service_policies_[service].default_policy =
        GetUserSet(rpc_policies.default_service_policy(), label_policies);
  }
}

absl::Status GrpcAuthzPolicyChecker::Check(const std::string& service,
                                           const std::string& rpc,
                                           const std::string& username) const {
  bool deny = false;
  if (service_policies_.count(service) == 0) {
    deny = true;
  } else if (service_policies_.at(service).rpc_policies.count(rpc) == 0) {
    deny = (service_policies_.at(service).default_policy.count(username) == 0);
  } else {
    deny = (service_policies_.at(service).rpc_policies.at(rpc).count(
                username) == 0);
  }

  if (deny) {
    LOG(INFO) << "Authz Policy: username " << username
              << " is not authorized for " << service << "/" << rpc;
    return gutil::PermissionDeniedErrorBuilder()
           << "Username " << username << " is not authorized for " << service
           << "/" << rpc;
  }
  return absl::OkStatus();
}

}  // namespace grpc_authz_processor
}  // namespace p4rt_app

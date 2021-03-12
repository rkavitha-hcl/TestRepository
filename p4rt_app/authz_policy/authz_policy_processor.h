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

#ifndef AUTHZ_POLICY_AUTHZ_POLICY_PROCESSOR_H_
#define AUTHZ_POLICY_AUTHZ_POLICY_PROCESSOR_H_

#include <memory>
#include <string>
#include <thread>  //NOLINT

#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"
#include "p4rt_app/authz_policy/authz_policy_checker.h"

namespace p4rt_app {
namespace grpc_authz_processor {

class GrpcAuthzPolicyProcessor : public grpc::AuthMetadataProcessor {
 public:
  GrpcAuthzPolicyProcessor(absl::string_view file_path, absl::string_view file);
  ~GrpcAuthzPolicyProcessor() = default;

  // Informs gRPC to schedule invocation of Process() in the same thread as
  // the one processing the client call.
  bool IsBlocking() const override { return false; }

  grpc::Status Process(
      const grpc::AuthMetadataProcessor::InputMetadata& auth_metadata,
      grpc::AuthContext* context,
      grpc::AuthMetadataProcessor::OutputMetadata* consumed_auth_metadata,
      grpc::AuthMetadataProcessor::OutputMetadata* response_metadata) override;

 private:
  // Parses the authz policy configuration file and saves it in cache.
  void FileRefresh();
  // This function runs in a detached thread. It monitors the change in the
  // authz policy configuration file and call FileRefresh().
  void FileRefreshLoop();

  std::string watched_dir_;
  std::string filename_;
  std::string absolute_file_path_;
  std::thread file_refresh_thread_;
  mutable absl::Mutex checker_lock_;
  std::unique_ptr<GrpcAuthzPolicyChecker> authz_policy_checker_
      ABSL_GUARDED_BY(checker_lock_);
};

}  // namespace grpc_authz_processor
}  // namespace p4rt_app

#endif  // AUTHZ_POLICY_AUTHZ_POLICY_PROCESSOR_H_

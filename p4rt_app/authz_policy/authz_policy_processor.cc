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

#include "p4rt_app/authz_policy/authz_policy_processor.h"

#include <fcntl.h>
#include <sys/inotify.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"
#include "gutil/status.h"
#include "p4rt_app/proto/authorization_policy.pb.h"

namespace p4rt_app {
namespace grpc_authz_processor {
namespace {

constexpr char kGrpcPathKey[] = ":path";

// Parses the service name and method name from a gRPC path. For example,
// "/foo.bar.RpcService/GetErrorStats" will return service name "RpcService" and
// method name "GetErrorStats".
absl::Status GetServiceAndMethod(absl::string_view path, std::string* service,
                                 std::string* method) {
  RET_CHECK(service != nullptr) << "service cannot be nullptr.";
  RET_CHECK(method != nullptr) << "method cannot be nullptr.";
  std::size_t method_pos = path.find_last_of('/', std::string::npos);
  std::size_t service_pos = path.find_last_of('.', std::string::npos);
  if (method_pos == std::string::npos || service_pos == std::string::npos ||
      method_pos <= service_pos + 1) {
    return gutil::InvalidArgumentErrorBuilder() << "Invalid RPC path: " << path;
  }
  *method = std::string(path.substr(method_pos + 1));
  *service =
      std::string(path.substr(service_pos + 1, method_pos - service_pos - 1));
  return absl::OkStatus();
}

// Parses the username from a SPIFFE ID.
// An example of SPIFFE ID looks like the following:
// "spiffe://public-borgmaster.campus-xxx.prod.google.com/prod_role/network-telemetry-pictor-sandbox-be-jobs"
// The username is after the last /.
absl::StatusOr<std::string> GetUsernameFromSpiffeId(
    absl::string_view spiffe_id) {
  std::size_t pos = spiffe_id.find_last_of('/', std::string::npos);
  if (pos == std::string::npos) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Invalid SPIFFE ID: " << spiffe_id;
  }
  return std::string(spiffe_id.substr(pos + 1));
}

}  // namespace

GrpcAuthzPolicyProcessor::GrpcAuthzPolicyProcessor(absl::string_view file_path,
                                                   absl::string_view file)
    : watched_dir_(file_path),
      filename_(file),
      absolute_file_path_(absl::StrCat(watched_dir_, "/", filename_)),
      file_refresh_thread_(&GrpcAuthzPolicyProcessor::FileRefreshLoop, this) {
  FileRefresh();
}

void GrpcAuthzPolicyProcessor::FileRefresh() {
  LOG(INFO) << "Updating authz policy. Parsing file: " << absolute_file_path_;
  int fileDescriptor = open(absolute_file_path_.c_str(), O_RDONLY);
  if (fileDescriptor < 0) {
    LOG(ERROR) << "Failed to open file: " << absolute_file_path_;
    return;
  }
  google::protobuf::io::FileInputStream fileInput(fileDescriptor);
  fileInput.SetCloseOnDelete(true);
  google::p4rt::AuthorizationPolicy authz_policy;
  if (!google::protobuf::TextFormat::Parse(&fileInput, &authz_policy)) {
    LOG(ERROR) << "Failed to parse file: " << absolute_file_path_;
    return;
  }
  absl::WriterMutexLock l(&checker_lock_);
  authz_policy_checker_ =
      absl::make_unique<GrpcAuthzPolicyChecker>(authz_policy);
}

void GrpcAuthzPolicyProcessor::FileRefreshLoop() {
  int inotify_fd = inotify_init();
  int watched_wd = inotify_add_watch(inotify_fd, absolute_file_path_.c_str(),
                                     IN_ATTRIB | IN_MODIFY);
  // If the file doesn't exist, the above wd fails to be created. This allows
  // us to create it if we see it anyway.
  int containing_wd = inotify_add_watch(
      inotify_fd, watched_dir_.c_str(),
      IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVE | IN_MODIFY);

  constexpr int kNameMaxChar = 255;
  // The max size of one inotify event, see linux man page inotify(7).
  constexpr size_t kEventBufLen =
      sizeof(inotify_event) + sizeof(char) * (kNameMaxChar + 1);
  char buf[kEventBufLen * 2];
  while (true) {
    int buflen = read(inotify_fd, buf, sizeof(buf));
    if (buflen == -1) {
      LOG(ERROR) << "inotify_fd is closed in authz policy processor.";
      return;
    }
    const struct inotify_event* event;
    bool need_refresh = false;
    for (char* ptr = buf; ptr < buf + buflen;
         ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      std::string event_name;
      if (event->len) {
        event_name = std::string(event->name);
      }
      if (event->wd == containing_wd && event_name == filename_) {
        // In case the file we are interested in was deleted or created.
        if (event->mask & IN_DELETE) {
          inotify_rm_watch(inotify_fd, watched_wd);
          watched_wd = -1;
        } else if (event->mask & IN_CREATE) {
          watched_wd = inotify_add_watch(
              inotify_fd, absolute_file_path_.c_str(), IN_ATTRIB | IN_MODIFY);
        }
        need_refresh = true;
      } else if (event->wd == watched_wd) {
        // For file changes.
        need_refresh = true;
      }
    }
    if (need_refresh) {
      FileRefresh();
    }
  }
}

grpc::Status GrpcAuthzPolicyProcessor::Process(
    const grpc::AuthMetadataProcessor::InputMetadata& auth_metadata,
    grpc::AuthContext* context,
    grpc::AuthMetadataProcessor::OutputMetadata* /*consumed_auth_metadata*/,
    grpc::AuthMetadataProcessor::OutputMetadata* /*response_metadata*/) {
  if (!context->IsPeerAuthenticated()) {
    LOG(ERROR) << __func__ << ": Access denied, client not authenticated.";
    return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                        "Unauthenticated user.");
  }

  auto auth_rpc_path = auth_metadata.find(kGrpcPathKey);
  if (auth_rpc_path == auth_metadata.end()) {
    // Deal with missing RPC method path in auth metadata.
    LOG(ERROR) << __func__ << ": Missing auth metadata " << kGrpcPathKey
               << ", RPC name not found in auth metadata.";
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "RPC path missing from auth metadata");
  }
  const std::string path(auth_rpc_path->second.data(),
                         auth_rpc_path->second.length());
  std::string method;
  std::string service;
  auto status = GetServiceAndMethod(path, &service, &method);
  if (!status.ok()) {
    return gutil::AbslStatusToGrpcStatus(status);
  }

  std::vector<grpc::string_ref> property_values;
  if (context->GetPeerIdentityPropertyName() == GRPC_X509_SAN_PROPERTY_NAME) {
    property_values = context->GetPeerIdentity();
  } else {
    property_values = context->FindPropertyValues(GRPC_X509_SAN_PROPERTY_NAME);
  }
  // We are expecting only 1 identity from AuthContext.
  if (property_values.size() != 1) {
    return grpc::Status(
        grpc::StatusCode::INTERNAL,
        absl::StrCat("Expecting 1 but got ", property_values.size(),
                     " x509 SAN properties."));
  }
  const std::string spiffe_id = property_values.front().data();
  auto username = GetUsernameFromSpiffeId(spiffe_id);
  if (!username.ok()) {
    return gutil::AbslStatusToGrpcStatus(username.status());
  }

  absl::ReaderMutexLock l(&checker_lock_);
  if (authz_policy_checker_ == nullptr) {
    LOG(INFO) << "Authz Policy: username " << *username
              << " is not authorized for " << service << "/" << method
              << " due to empty authz policy config";
    return grpc::Status(grpc::StatusCode::PERMISSION_DENIED,
                        "Empty authz policy config");
  }
  return gutil::AbslStatusToGrpcStatus(
      authz_policy_checker_->Check(service, method, *username));
}

}  // namespace grpc_authz_processor
}  // namespace p4rt_app

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

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "authz_policy/authz_policy_processor.h"
#include "glog/logging.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/security/tls_certificate_provider.h"
#include "grpcpp/security/tls_credentials_options.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "gutil/status.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/sonic/adapters/system_call_adapter.h"
#include "p4rt_app/sonic/packetio_impl.h"
#include "swss/component_state_helper.h"
#include "swss/component_state_helper_interface.h"
#include "swss/consumernotifier.h"
#include "swss/dbconnector.h"
#include "swss/producerstatetable.h"
#include "swss/schema.h"

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerCredentials;

DEFINE_bool(use_insecure_server_credentials, false, "Insecure gRPC.");
DEFINE_string(ca_certificate_file, "",
              "CA root certificate file, in PEM format. If set, p4rt will "
              "require and verify client certificate.");
DEFINE_string(server_certificate_file, "",
              "Server certificate file, in PEM format.");
DEFINE_string(server_key_file, "", "Server key file, in PEM format.");
DEFINE_bool(
    authz_policy_enabled, false,
    "Enable authz policy. Only take effect if use_insecure_server_credentials "
    "is false and mTLS is configured.");
DEFINE_string(authorization_policy_file_path, "/keys",
              "Path prefix of the authorization policy file.");
DEFINE_string(authorization_policy_file_name, "authorization_policy.proto.txt",
              "File name of the authorization policy file.");
DEFINE_bool(use_genetlink, false,
            "Enable Generic Netlink model for Packet Receive");

absl::StatusOr<std::shared_ptr<ServerCredentials>> BuildServerCredentials() {
  constexpr int kCertRefreshIntervalSec = 5;
  constexpr char kRootCertName[] = "root_cert";
  constexpr char kIdentityCertName[] = "switch_cert";

  std::shared_ptr<ServerCredentials> creds;
  if (FLAGS_use_insecure_server_credentials || FLAGS_server_key_file.empty() ||
      FLAGS_server_certificate_file.empty()) {
    creds = grpc::InsecureServerCredentials();
    if (creds == nullptr) {
      return gutil::InternalErrorBuilder()
             << "nullptr returned from grpc::InsecureServerCredentials";
    }
  } else {
    // If CA certificate is not provided, client certificate is not required.
    if (FLAGS_ca_certificate_file.empty()) {
      auto certificate_provider =
          std::make_shared<grpc::experimental::FileWatcherCertificateProvider>(
              FLAGS_server_key_file, FLAGS_server_certificate_file,
              kCertRefreshIntervalSec);
      grpc::experimental::TlsServerCredentialsOptions opts(
          certificate_provider);
      opts.watch_identity_key_cert_pairs();
      opts.set_identity_cert_name(kIdentityCertName);
      opts.set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
      creds = grpc::experimental::TlsServerCredentials(opts);
    } else {
      auto certificate_provider =
          std::make_shared<grpc::experimental::FileWatcherCertificateProvider>(
              FLAGS_server_key_file, FLAGS_server_certificate_file,
              FLAGS_ca_certificate_file, kCertRefreshIntervalSec);
      grpc::experimental::TlsServerCredentialsOptions opts(
          certificate_provider);
      opts.watch_root_certs();
      opts.set_root_cert_name(kRootCertName);
      opts.watch_identity_key_cert_pairs();
      opts.set_identity_cert_name(kIdentityCertName);
      opts.set_cert_request_type(
          GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
      creds = grpc::experimental::TlsServerCredentials(opts);
    }
    if (creds == nullptr) {
      return gutil::InternalErrorBuilder()
             << "nullptr returned from grpc::SslServerCredentials";
    }

    if (FLAGS_authz_policy_enabled && !FLAGS_ca_certificate_file.empty()) {
      auto authz_policy_processor = std::make_shared<
          p4rt_app::grpc_authz_processor::GrpcAuthzPolicyProcessor>(
          FLAGS_authorization_policy_file_path,
          FLAGS_authorization_policy_file_name);
      creds->SetAuthMetadataProcessor(authz_policy_processor);
    }
  }
  return creds;
}

int main(int argc, char** argv) {
  constexpr char kRedisDbHost[] = "localhost";
  constexpr int kRedisDbPort = 6379;
  constexpr char kServerAddress[] = "[::]:9559";
  constexpr char kInternalServerAddress[] = "unix:/sock/p4rt.sock";

  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Open a database connection into the SONiC AppDb.
  auto sonic_app_db =
      absl::make_unique<swss::DBConnector>(APPL_DB, kRedisDbHost, kRedisDbPort,
                                           /*timeout=*/0);

  // Open a database connection into the SONiC StateDb.
  auto sonic_state_db = absl::make_unique<swss::DBConnector>(
      APPL_STATE_DB, kRedisDbHost, kRedisDbPort,
      /*timeout=*/0);

  // Open a database connection into the SONiC CountersDb
  auto sonic_counters_db = absl::make_unique<swss::DBConnector>(
      COUNTERS_DB, kRedisDbHost, kRedisDbPort,
      /*timeout=*/0);

  // Create interfaces to interact with the AppDb P4RT table.
  auto app_db_table_p4rt =
      absl::make_unique<swss::ProducerStateTable>(sonic_app_db.get(), "P4RT");
  auto notification_channel_p4rt = absl::make_unique<swss::ConsumerNotifier>(
      "APPL_DB_P4RT_RESPONSE_CHANNEL", sonic_app_db.get());

  // Create interfaces to interact with the AppDb VRF table.
  auto app_db_table_vrf = absl::make_unique<swss::ProducerStateTable>(
      sonic_app_db.get(), "VRF_TABLE");
  auto notification_channel_vrf = absl::make_unique<swss::ConsumerNotifier>(
      "APPL_DB_VRF_TABLE_RESPONSE_CHANNEL", sonic_app_db.get());

  // Create interfaces to interact with the AppDb HASH table.
  auto app_db_table_hash = absl::make_unique<swss::ProducerStateTable>(
      sonic_app_db.get(), "HASH_TABLE");
  auto notification_channel_hash = absl::make_unique<swss::ConsumerNotifier>(
      "APPL_DB_HASH_TABLE_RESPONSE_CHANNEL", sonic_app_db.get());

  // Create interfaces to interact with the AppDb SWITCH table.
  auto app_db_table_switch = absl::make_unique<swss::ProducerStateTable>(
      sonic_app_db.get(), "SWITCH_TABLE");
  auto notification_channel_switch = absl::make_unique<swss::ConsumerNotifier>(
      "APPL_DB_SWITCH_TABLE_RESPONSE_CHANNEL", sonic_app_db.get());

  // Wait for PortInitDone to be done.
  p4rt_app::sonic::WaitForPortInitDone(*sonic_app_db);

  // Create PacketIoImpl that will auto discover the ports.
  auto packetio_impl_or = p4rt_app::sonic::PacketIoImpl::CreatePacketIoImpl();
  if (!packetio_impl_or.ok()) {
    LOG(ERROR) << "Couldnt discover Packet I/O ports, error: "
               << packetio_impl_or.status();
    return -1;
  }

  // Get the system state helper which will be used to verify the switch is
  // healthy, and not in a critical state before handling P4 Runtime requests.
  swss::SystemStateHelperInterface& system_state_singleton =
      swss::StateHelperManager::SystemSingleton();

  // Create the P4RT server.
  p4rt_app::P4RuntimeImpl p4runtime_server(
      std::move(sonic_app_db), std::move(sonic_state_db),
      std::move(sonic_counters_db), std::move(app_db_table_p4rt),
      std::move(notification_channel_p4rt), std::move(app_db_table_vrf),
      std::move(notification_channel_vrf), std::move(app_db_table_hash),
      std::move(notification_channel_hash), std::move(app_db_table_switch),
      std::move(notification_channel_switch), std::move(*packetio_impl_or),
      system_state_singleton, FLAGS_use_genetlink);

  // Start a P4 runtime server
  ServerBuilder builder;
  auto server_cred = BuildServerCredentials();
  if (!server_cred.ok()) {
    LOG(ERROR) << "Failed to build server credentials, error "
               << server_cred.status();
    return -1;
  }
  builder.AddListeningPort(kServerAddress, *server_cred);
  builder.AddListeningPort(kInternalServerAddress,
                           grpc::InsecureServerCredentials());
  builder.RegisterService(&p4runtime_server);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  LOG(INFO) << "Server listening on " << kServerAddress << ".";
  server->Wait();

  return 0;
}

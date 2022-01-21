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
#include <thread>  // NOLINT

#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"
#include "grpc/impl/codegen/grpc_types.h"
#include "grpcpp/security/authorization_policy_provider.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/security/tls_certificate_provider.h"
#include "grpcpp/security/tls_credentials_options.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "gutil/status.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4rt_app/event_monitoring/port_change_events.h"
#include "p4rt_app/event_monitoring/state_event_monitor.h"
#include "p4rt_app/event_monitoring/state_verification_events.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/sonic/adapters/consumer_notifier_adapter.h"
#include "p4rt_app/sonic/adapters/db_connector_adapter.h"
#include "p4rt_app/sonic/adapters/producer_state_table_adapter.h"
#include "p4rt_app/sonic/adapters/system_call_adapter.h"
#include "p4rt_app/sonic/packetio_impl.h"
#include "swss/component_state_helper.h"
#include "swss/component_state_helper_interface.h"
#include "swss/dbconnector.h"
#include "swss/schema.h"

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerCredentials;

// By default the P4RT App will run on TCP port 9559. Which is the IANA port
// reserved for P4Runtime.
// https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml?search=9559
DEFINE_int32(p4rt_grpc_port, 9559, "gRPC port for the P4Runtime Server");

// Optionally, the P4RT App can be run on a unix socket, but the connection will
// be insecure.
DEFINE_string(
    p4rt_unix_socket, "/sock/p4rt.sock",
    "Unix socket file for internal insecure connections. Disabled if empty.");

// Runtime flags to configure any server credentials for secure conections.
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
DEFINE_string(authorization_policy_file, "/keys/authorization_policy.json",
              "File name of the JSON authorization policy file.");

// P4Runtime options:
DEFINE_bool(use_genetlink, false,
            "Enable Generic Netlink model for Packet Receive");
DEFINE_bool(use_port_ids, false,
            "Controller will use Port ID values configured over gNMI instead "
            "of the SONiC interface names.");
DEFINE_string(save_forwarding_config_file,
              "/etc/sonic/p4rt_forwarding_config.pb.txt",
              "Saves the forwarding pipeline config to a file so it can be "
              "reloaded after reboot.");

// We expect to run in the SONiC enviornment so the RedisDb connections are
// fixed, and connot be set by a flag.
constexpr char kRedisDbHost[] = "localhost";
constexpr int kRedisDbPort = 6379;

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
  }
  return creds;
}

std::shared_ptr<grpc::experimental::AuthorizationPolicyProviderInterface>
CreateAuthzPolicyProvider() {
  constexpr int kAuthzRefreshIntervalSec = 5;
  grpc::Status status;
  auto provider =
      grpc::experimental::FileWatcherAuthorizationPolicyProvider::Create(
          FLAGS_authorization_policy_file, kAuthzRefreshIntervalSec, &status);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to create authorization provider: "
               << gutil::GrpcStatusToAbslStatus(status);
    return nullptr;
  }
  return provider;
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Get the P4RT component helper which can be used to put the switch into
  // critical state.
  swss::ComponentStateHelperInterface& component_state_singleton =
      swss::StateHelperManager::ComponentSingleton(
          swss::SystemComponent::kP4rt);

  // Get the system state helper which will be used to verify the switch is
  // healthy, and not in a critical state before handling P4 Runtime requests.
  swss::SystemStateHelperInterface& system_state_singleton =
      swss::StateHelperManager::SystemSingleton();

  // Open a database connection into the SONiC AppDb.
  swss::DBConnector app_db(APPL_DB, kRedisDbHost, kRedisDbPort, /*timeout=*/0);
  auto sonic_app_db =
      absl::make_unique<p4rt_app::sonic::DBConnectorAdapter>(&app_db);

  // Open a database connection into the SONiC AppStateDb.
  swss::DBConnector app_state_db(APPL_STATE_DB, kRedisDbHost, kRedisDbPort,
                                 /*timeout=*/0);
  auto sonic_app_state_db =
      absl::make_unique<p4rt_app::sonic::DBConnectorAdapter>(&app_state_db);

  // Open a database connection into the SONiC CountersDb
  swss::DBConnector counters_db(COUNTERS_DB, kRedisDbHost, kRedisDbPort,
                                /*timeout=*/0);
  auto sonic_counters_db =
      absl::make_unique<p4rt_app::sonic::DBConnectorAdapter>(&counters_db);

  // Create interfaces to interact with the AppDb P4RT table.
  auto app_db_table_p4rt =
      absl::make_unique<p4rt_app::sonic::ProducerStateTableAdapter>(
          &app_db, APP_P4RT_TABLE_NAME);
  auto notification_channel_p4rt =
      absl::make_unique<p4rt_app::sonic::ConsumerNotifierAdapter>(
          "APPL_DB_P4RT_RESPONSE_CHANNEL", &app_db);

  // Create interfaces to interact with the AppDb VRF table.
  auto app_db_table_vrf =
      absl::make_unique<p4rt_app::sonic::ProducerStateTableAdapter>(
          &app_db, APP_VRF_TABLE_NAME);
  auto notification_channel_vrf =
      absl::make_unique<p4rt_app::sonic::ConsumerNotifierAdapter>(
          "APPL_DB_VRF_TABLE_RESPONSE_CHANNEL", &app_db);

  // Create interfaces to interact with the AppDb HASH table.
  auto app_db_table_hash =
      absl::make_unique<p4rt_app::sonic::ProducerStateTableAdapter>(
          &app_db, "HASH_TABLE");
  auto notification_channel_hash =
      absl::make_unique<p4rt_app::sonic::ConsumerNotifierAdapter>(
          "APPL_DB_HASH_TABLE_RESPONSE_CHANNEL", &app_db);

  // Create interfaces to interact with the AppDb SWITCH table.
  auto app_db_table_switch =
      absl::make_unique<p4rt_app::sonic::ProducerStateTableAdapter>(
          &app_db, "SWITCH_TABLE");
  auto notification_channel_switch =
      absl::make_unique<p4rt_app::sonic::ConsumerNotifierAdapter>(
          "APPL_DB_SWITCH_TABLE_RESPONSE_CHANNEL", &app_db);

  // Create PacketIoImpl for Packet I/O.
  auto packetio_impl = p4rt_app::sonic::PacketIoImpl::CreatePacketIoImpl();

  // Wait for PortInitDone to be done.
  p4rt_app::sonic::WaitForPortInitDone(*sonic_app_db);

  // Configure the P4RT options.
  p4rt_app::P4RuntimeImplOptions p4rt_options{
      .use_genetlink = FLAGS_use_genetlink,
      .translate_port_ids = FLAGS_use_port_ids,
  };

  std::string save_forwarding_config_file = FLAGS_save_forwarding_config_file;
  if (!save_forwarding_config_file.empty()) {
    p4rt_options.forwarding_config_full_path = save_forwarding_config_file;
  }

  // Create the P4RT server.
  p4rt_app::P4RuntimeImpl p4runtime_server(
      std::move(sonic_app_db), std::move(sonic_app_state_db),
      std::move(sonic_counters_db), std::move(app_db_table_p4rt),
      std::move(notification_channel_p4rt), std::move(app_db_table_vrf),
      std::move(notification_channel_vrf), std::move(app_db_table_hash),
      std::move(notification_channel_hash), std::move(app_db_table_switch),
      std::move(notification_channel_switch), std::move(packetio_impl),
      component_state_singleton, system_state_singleton, p4rt_options);

  // Create a server to listen on the unix socket port.
  std::thread internal_server_thread;
  if (!FLAGS_p4rt_unix_socket.empty()) {
    internal_server_thread = std::thread(
        [](p4rt_app::P4RuntimeImpl* p4rt_server) {
          ServerBuilder builder;
          builder.AddListeningPort(
              absl::StrCat("unix:", FLAGS_p4rt_unix_socket),
              grpc::InsecureServerCredentials());
          builder.RegisterService(p4rt_server);
          std::unique_ptr<Server> server(builder.BuildAndStart());
          LOG(INFO) << "Started unix socket server listening on "
                    << FLAGS_p4rt_unix_socket << ".";
          server->Wait();
        },
        &p4runtime_server);
  }

  // Spawn a separate thread that can react to any port change events.
  bool monitor_port_events = true;
  auto port_events_thread =
      std::thread([&p4runtime_server, &monitor_port_events]() {
        swss::DBConnector state_db(APPL_STATE_DB, kRedisDbHost, kRedisDbPort,
                                   /*timeout=*/0);
        p4rt_app::sonic::StateEventMonitor port_state_monitor(&state_db,
                                                              "PORT_TABLE");
        p4rt_app::PortChangeEvents port_event_handler(p4runtime_server,
                                                      port_state_monitor);

        // Continue to monitor port events for the life of the P4RT service.
        while (monitor_port_events) {
          absl::Status status =
              port_event_handler.WaitForEventAndUpdateP4Runtime();
          if (!status.ok()) {
            LOG(ERROR) << status;
          }
        }
      });

  // Start listening for state verification events, and update StateDb for P4RT.
  swss::DBConnector state_verification_db(STATE_DB, kRedisDbHost, kRedisDbPort,
                                          /*timeout=*/0);
  p4rt_app::sonic::DBConnectorAdapter state_verification_db_adapter(
      &state_verification_db);
  p4rt_app::sonic::ConsumerNotifierAdapter state_verification_notifier(
      "VERIFY_STATE_REQ_CHANNEL", &state_verification_db);
  p4rt_app::StateVerificationEvents state_verification_event_monitor(
      p4runtime_server, state_verification_notifier,
      state_verification_db_adapter);
  state_verification_event_monitor.Start();

  // Start a P4 runtime server
  ServerBuilder builder;
  auto server_cred = BuildServerCredentials();
  if (!server_cred.ok()) {
    LOG(ERROR) << "Failed to build server credentials, error "
               << server_cred.status();
    return -1;
  }

  std::string server_addr = absl::StrCat("[::]:", FLAGS_p4rt_grpc_port);
  builder.AddListeningPort(server_addr, *server_cred);

  // Set authorization policy.
  if (FLAGS_authz_policy_enabled && !FLAGS_ca_certificate_file.empty()) {
    auto provider = CreateAuthzPolicyProvider();
    if (provider == nullptr) {
      LOG(ERROR) << "Error in creating authz policy provider";
      return -1;
    }
    builder.experimental().SetAuthorizationPolicyProvider(std::move(provider));
  }

  builder.RegisterService(&p4runtime_server);

  // Disable max ping strikes behavior to allow more frequent KA.
  builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 0);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  LOG(INFO) << "Server listening on " << server_addr << ".";
  server->Wait();

  LOG(INFO) << "Stopping monitoring of port events.";
  monitor_port_events = false;
  port_events_thread.join();

  return 0;
}

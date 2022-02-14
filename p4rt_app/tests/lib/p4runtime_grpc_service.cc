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
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "gutil/status_matchers.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/sonic/adapters/fake_consumer_notifier_adapter.h"
#include "p4rt_app/sonic/adapters/fake_db_connector_adapter.h"
#include "p4rt_app/sonic/adapters/fake_producer_state_table_adapter.h"
#include "p4rt_app/sonic/adapters/fake_sonic_db_table.h"
#include "p4rt_app/sonic/fake_packetio_interface.h"
#include "p4rt_app/sonic/redis_connections.h"
#include "swss/consumerstatetable.h"
#include "swss/dbconnector.h"
#include "swss/fakes/fake_component_state_helper.h"
#include "swss/fakes/fake_system_state_helper.h"
#include "swss/notificationproducer.h"

namespace p4rt_app {
namespace test_lib {

P4RuntimeGrpcService::P4RuntimeGrpcService(
    const P4RuntimeImplOptions& options) {
  LOG(INFO) << "Starting the P4 runtime gRPC service.";
  const std::string kP4rtTableName = "P4RT";
  const std::string kPortTableName = "PORT_TABLE";
  const std::string kVrfTableName = "VRF_TABLE";
  const std::string kHashTableName = "HASH_TABLE";
  const std::string kSwitchTableName = "SWITCH_TABLE";
  const std::string kCountersTableName = "COUNTERS";

  // Choose a random gRPC port. While not strictly necessary each test brings up
  // a new gRPC service, and randomly choosing a TCP port will minimize issues.
  absl::BitGen gen;
  grpc_port_ = absl::Uniform<int>(gen, 49152, 65535);

  // Connect SONiC AppDB tables with their equivelant AppStateDB tables.
  fake_p4rt_table_ = sonic::FakeSonicDbTable(&fake_p4rt_state_table_);
  fake_vrf_table_ = sonic::FakeSonicDbTable(&fake_vrf_state_table_);
  fake_hash_table_ = sonic::FakeSonicDbTable(&fake_hash_state_table_);
  fake_switch_table_ = sonic::FakeSonicDbTable(&fake_switch_state_table_);

  // Create interfaces to access P4RT_TABLE entries.
  auto fake_p4rt_app_db = absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto fake_p4rt_app_state_db =
      absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto fake_p4rt_counter_db =
      absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  fake_p4rt_app_db->AddSonicDbTable(kP4rtTableName, &fake_p4rt_table_);
  fake_p4rt_app_state_db->AddSonicDbTable(kP4rtTableName,
                                          &fake_p4rt_state_table_);
  fake_p4rt_app_db->AddSonicDbTable(kVrfTableName, &fake_vrf_table_);
  fake_p4rt_app_state_db->AddSonicDbTable(kVrfTableName,
                                          &fake_vrf_state_table_);
  fake_p4rt_counter_db->AddSonicDbTable(kCountersTableName,
                                        &fake_p4rt_counters_table_);
  sonic::P4rtTable p4rt_table{
      .producer_state = std::make_unique<sonic::FakeProducerStateTableAdapter>(
          kP4rtTableName, &fake_p4rt_table_),
      .notifier = absl::make_unique<sonic::FakeConsumerNotifierAdapter>(
          &fake_p4rt_table_),
      .app_db = std::move(fake_p4rt_app_db),
      .app_state_db = std::move(fake_p4rt_app_state_db),
      .counter_db = std::move(fake_p4rt_counter_db),
  };

  // Create interfaces to access VRF_TABLE entries.
  auto fake_vrf_app_db = absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto fake_vrf_app_state_db =
      absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  fake_vrf_app_db->AddSonicDbTable(kVrfTableName, &fake_vrf_table_);
  fake_vrf_app_state_db->AddSonicDbTable(kVrfTableName, &fake_vrf_state_table_);
  sonic::VrfTable vrf_table{
      .producer_state = std::make_unique<sonic::FakeProducerStateTableAdapter>(
          kVrfTableName, &fake_vrf_table_),
      .notifier = absl::make_unique<sonic::FakeConsumerNotifierAdapter>(
          &fake_vrf_table_),
      .app_db = std::move(fake_vrf_app_db),
      .app_state_db = std::move(fake_vrf_app_state_db),
  };

  // Create interfaces to access HASH_TABLE entries.
  auto fake_hash_app_db = absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto fake_hash_app_state_db =
      absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  fake_hash_app_db->AddSonicDbTable(kHashTableName, &fake_hash_table_);
  fake_hash_app_state_db->AddSonicDbTable(kHashTableName,
                                          &fake_hash_state_table_);
  sonic::HashTable hash_table{
      .producer_state = std::make_unique<sonic::FakeProducerStateTableAdapter>(
          kHashTableName, &fake_hash_table_),
      .notifier = absl::make_unique<sonic::FakeConsumerNotifierAdapter>(
          &fake_hash_table_),
      .app_db = std::move(fake_hash_app_db),
      .app_state_db = std::move(fake_hash_app_state_db),
  };

  // Create interfaces to access SWITCH_TABLE entries.
  auto fake_switch_app_db =
      absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto fake_switch_app_state_db =
      absl::make_unique<sonic::FakeDBConnectorAdapter>(":");
  fake_switch_app_db->AddSonicDbTable(kSwitchTableName, &fake_switch_table_);
  fake_switch_app_state_db->AddSonicDbTable(kSwitchTableName,
                                            &fake_switch_state_table_);
  sonic::SwitchTable switch_table{
      .producer_state = std::make_unique<sonic::FakeProducerStateTableAdapter>(
          kSwitchTableName, &fake_switch_table_),
      .notifier = absl::make_unique<sonic::FakeConsumerNotifierAdapter>(
          &fake_switch_table_),
      .app_db = std::move(fake_switch_app_db),
      .app_state_db = std::move(fake_switch_app_state_db),
  };

  // Create FakePacketIoInterface and save the pointer.
  auto fake_packetio_interface =
      absl::make_unique<sonic::FakePacketIoInterface>();
  fake_packetio_interface_ = fake_packetio_interface.get();

  // Add the P4RT component helper into the system state helper so they can
  // interact around critical state handling.
  fake_system_state_helper_.AddComponent(/*name=*/"p4rt-con",
                                         fake_component_state_helper_);

  // Create the P4RT server.
  p4runtime_server_ = absl::make_unique<P4RuntimeImpl>(
      std::move(p4rt_table), std::move(vrf_table), std::move(hash_table),
      std::move(switch_table), std::move(fake_packetio_interface),
      fake_component_state_helper_, fake_system_state_helper_, options);

  // Component tests will use an insecure connection for the service.
  std::string server_address = absl::StrCat("localhost:", GrpcPort());
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();

  // Finally start the gRPC service.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, creds);
  builder.RegisterService(p4runtime_server_.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

  LOG(INFO) << "Server listening on " << server_address;
  server_ = std::move(server);
}

P4RuntimeGrpcService::~P4RuntimeGrpcService() {
  LOG(INFO) << "Stopping the P4 runtime gRPC service.";
  if (server_) server_->Shutdown();
}

int P4RuntimeGrpcService::GrpcPort() const { return grpc_port_; }

absl::Status P4RuntimeGrpcService::AddPortTranslation(
    const std::string& port_name, const std::string& port_id) {
  return p4runtime_server_->AddPortTranslation(port_name, port_id);
}

absl::Status P4RuntimeGrpcService::RemovePortTranslation(
    const std::string& port_name) {
  return p4runtime_server_->RemovePortTranslation(port_name);
}

absl::Status P4RuntimeGrpcService::VerifyState() {
  return p4runtime_server_->VerifyState();
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetP4rtAppDbTable() {
  return fake_p4rt_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetPortAppDbTable() {
  return fake_port_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetVrfAppDbTable() {
  return fake_vrf_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetHashAppDbTable() {
  return fake_hash_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetSwitchAppDbTable() {
  return fake_switch_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetP4rtAppStateDbTable() {
  return fake_p4rt_state_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetVrfAppStateDbTable() {
  return fake_vrf_state_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetHashAppStateDbTable() {
  return fake_hash_state_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetSwitchAppStateDbTable() {
  return fake_switch_state_table_;
}

sonic::FakeSonicDbTable& P4RuntimeGrpcService::GetP4rtCountersDbTable() {
  return fake_p4rt_counters_table_;
}

sonic::FakePacketIoInterface& P4RuntimeGrpcService::GetFakePacketIoInterface() {
  return *fake_packetio_interface_;
}

swss::FakeSystemStateHelper& P4RuntimeGrpcService::GetSystemStateHelper() {
  return fake_system_state_helper_;
}

swss::FakeComponentStateHelper&
P4RuntimeGrpcService::GetComponentStateHelper() {
  return fake_component_state_helper_;
}

}  // namespace test_lib
}  // namespace p4rt_app

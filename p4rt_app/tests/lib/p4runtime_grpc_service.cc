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

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "grpcpp/security/server_credentials.h"
#include "gutil/status_matchers.h"
#include "swss/consumerstatetable.h"
#include "swss/dbconnector.h"
#include "swss/fakes/fake_consumer_notifier.h"
#include "swss/fakes/fake_db_connector.h"
#include "swss/fakes/fake_producer_state_table.h"
#include "swss/notificationproducer.h"

namespace p4rt_app {
namespace test_lib {

P4RuntimeGrpcService::P4RuntimeGrpcService() {
  LOG(INFO) << "Starting the P4 runtime gRPC service.";
  const std::string kP4rtTableName = "P4RT";
  const std::string kPortTableName = "PORT_TABLE";
  const std::string kVrfTableName = "VRF_TABLE";
  const std::string kHashTableName = "HASH_TABLE";
  const std::string kSwitchTableName = "SWITCH_TABLE";

  // Create AppDb interfaces used by the P4RT App.
  auto fake_app_db_client = absl::make_unique<swss::FakeDBConnector>();
  fake_app_db_client->AddAppDbTable(kP4rtTableName, &fake_p4rt_table_);
  fake_app_db_client->AddAppDbTable(kPortTableName, &fake_port_table_);
  fake_app_db_client->AddAppDbTable(kVrfTableName, &fake_vrf_table_);
  fake_app_db_client->AddAppDbTable(kHashTableName, &fake_hash_table_);
  fake_app_db_client->AddAppDbTable(kSwitchTableName, &fake_switch_table_);

  // P4RT table.
  auto fake_app_db_table_p4rt = absl::make_unique<swss::FakeProducerStateTable>(
      kP4rtTableName, &fake_p4rt_table_);
  auto fake_notify_p4rt =
      absl::make_unique<swss::FakeConsumerNotifier>(&fake_p4rt_table_);

  // VRF_TABLE table.
  auto fake_app_db_table_vrf = absl::make_unique<swss::FakeProducerStateTable>(
      kVrfTableName, &fake_vrf_table_);
  auto fake_notify_vrf =
      absl::make_unique<swss::FakeConsumerNotifier>(&fake_vrf_table_);

  // HASH_TABLE table.
  auto fake_app_db_table_hash = absl::make_unique<swss::FakeProducerStateTable>(
      kHashTableName, &fake_hash_table_);
  auto fake_notify_hash =
      absl::make_unique<swss::FakeConsumerNotifier>(&fake_hash_table_);

  // SWITCH_TABLE table.
  auto fake_app_db_table_switch =
      absl::make_unique<swss::FakeProducerStateTable>(kSwitchTableName,
                                                      &fake_switch_table_);
  auto fake_notify_switch =
      absl::make_unique<swss::FakeConsumerNotifier>(&fake_switch_table_);

  // Create StateDb interfaces used by the P4RT App.
  auto fake_state_db_client = absl::make_unique<swss::FakeDBConnector>();

  // Create FakePacketIoInterface and save the pointer.
  auto fake_packetio_interface =
      absl::make_unique<sonic::FakePacketIoInterface>();
  fake_packetio_interface_ = fake_packetio_interface.get();

  // Create the P4RT server.
  p4runtime_server_ = absl::make_unique<P4RuntimeImpl>(
      std::move(fake_app_db_client), std::move(fake_state_db_client),
      std::move(fake_app_db_table_p4rt), std::move(fake_notify_p4rt),
      std::move(fake_app_db_table_vrf), std::move(fake_notify_vrf),
      std::move(fake_app_db_table_hash), std::move(fake_notify_hash),
      std::move(fake_app_db_table_switch), std::move(fake_notify_switch),
      std::move(fake_packetio_interface));

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

int P4RuntimeGrpcService::GrpcPort() const { return 9999; }

swss::FakeSonicDbTable& P4RuntimeGrpcService::GetP4rtAppDbTable() {
  return fake_p4rt_table_;
}

swss::FakeSonicDbTable& P4RuntimeGrpcService::GetPortAppDbTable() {
  return fake_port_table_;
}

swss::FakeSonicDbTable& P4RuntimeGrpcService::GetVrfAppDbTable() {
  return fake_vrf_table_;
}

swss::FakeSonicDbTable& P4RuntimeGrpcService::GetHashAppDbTable() {
  return fake_hash_table_;
}

swss::FakeSonicDbTable& P4RuntimeGrpcService::GetSwitchAppDbTable() {
  return fake_switch_table_;
}

sonic::FakePacketIoInterface& P4RuntimeGrpcService::GetFakePacketIoInterface() {
  return *fake_packetio_interface_;
}

}  // namespace test_lib
}  // namespace p4rt_app

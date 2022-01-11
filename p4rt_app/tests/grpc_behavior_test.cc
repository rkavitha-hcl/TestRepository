// Copyright 2022 Google LLC
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
#include <grpcpp/support/status.h>

#include <memory>

#include "gmock/gmock.h"
#include "grpcpp/client_context.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "gtest/gtest.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/sonic/adapters/fake_consumer_notifier_adapter.h"
#include "p4rt_app/sonic/adapters/fake_db_connector_adapter.h"
#include "p4rt_app/sonic/adapters/fake_producer_state_table_adapter.h"
#include "p4rt_app/sonic/adapters/fake_sonic_db_table.h"
#include "p4rt_app/sonic/fake_packetio_interface.h"
#include "swss/fakes/fake_component_state_helper.h"
#include "swss/fakes/fake_system_state_helper.h"

namespace p4rt_app {
namespace {

constexpr char kServerAddr[] = "localhost:9999";

// This test suite doesn't deal with the P4Runtime service so we do not need to
// properly confiugre the fake DB connections.
P4RuntimeImpl DummyP4RuntimeImpl() {
  const std::string fake_name = "DUMMY_TABLE";
  sonic::FakeSonicDbTable fake_db_table;

  // Dummy redis DB clients.
  auto app_db_client = std::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto app_state_db_client =
      std::make_unique<sonic::FakeDBConnectorAdapter>(":");
  auto counter_db_client = std::make_unique<sonic::FakeDBConnectorAdapter>(":");

  // Dummy producer state tables.
  auto app_db_p4rt_table =
      std::make_unique<sonic::FakeProducerStateTableAdapter>(fake_name,
                                                             &fake_db_table);
  auto app_db_vrf_table =
      std::make_unique<sonic::FakeProducerStateTableAdapter>(fake_name,
                                                             &fake_db_table);
  auto app_db_hash_table =
      std::make_unique<sonic::FakeProducerStateTableAdapter>(fake_name,
                                                             &fake_db_table);
  auto app_db_switch_table =
      std::make_unique<sonic::FakeProducerStateTableAdapter>(fake_name,
                                                             &fake_db_table);

  // Dummy consumer notifiers.
  auto notify_p4rt_table =
      std::make_unique<sonic::FakeConsumerNotifierAdapter>(&fake_db_table);
  auto notify_vrf_table =
      std::make_unique<sonic::FakeConsumerNotifierAdapter>(&fake_db_table);
  auto notify_hash_table =
      std::make_unique<sonic::FakeConsumerNotifierAdapter>(&fake_db_table);
  auto notify_switch_table =
      std::make_unique<sonic::FakeConsumerNotifierAdapter>(&fake_db_table);

  // Dummy PacketIO.
  auto packet_io = std::make_unique<sonic::FakePacketIoInterface>();

  // Dummy state managment.
  swss::FakeComponentStateHelper component_state_helper;
  swss::FakeSystemStateHelper system_state_helper;

  return P4RuntimeImpl(
      std::move(app_db_client), std::move(app_state_db_client),
      std::move(counter_db_client), std::move(app_db_p4rt_table),
      std::move(notify_p4rt_table), std::move(app_db_vrf_table),
      std::move(notify_vrf_table), std::move(app_db_hash_table),
      std::move(notify_hash_table), std::move(app_db_switch_table),
      std::move(notify_switch_table), std::move(packet_io),
      component_state_helper, system_state_helper, P4RuntimeImplOptions{});
}

TEST(GrpcBehaviorTest,
     SendingKeepAliveWithoutDataWillCloseServerWithDefaultConfig) {
  P4RuntimeImpl dummy_service = DummyP4RuntimeImpl();

  // Configure the gRPC service using default values.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(kServerAddr, grpc::InsecureServerCredentials());
  builder.RegisterService(&dummy_service);

  // If we wanted to ignore all ping strikes due to excessive KEEPALIVE pings we
  // could disable the count on the server side. For example:
  //   builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 0);
  // In this case we would expect this test to run until timeout.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

  // https://github.com/grpc/grpc/blob/master/doc/keepalive.md
  //
  // We configure the client such that the KEEPALIVE pings are sent every 500
  // ms, and can be sent even if there is not data being sent over the channel.
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 500);
  channel_args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);

  // Open a stream channel to the gRPC service.
  auto channel = grpc::CreateCustomChannel(
      kServerAddr, grpc::InsecureChannelCredentials(), channel_args);
  auto p4rt_stub = p4::v1::P4Runtime::NewStub(channel);
  grpc::ClientContext client_context;
  auto client_stream = p4rt_stub->StreamChannel(&client_context);

  // By default the gRPC will allow 2 pings without data before it sends a HTTP2
  // GOAWAY frame and closes the connection. Since we send this ping every 500ms
  // we expect the test to take a few seconds (i.e. 2 * 500ms) before the stream
  // gets closed.
  p4::v1::StreamMessageResponse dummy_response;
  while (client_stream->Read(&dummy_response)) {
    // We do not expect a resposne since no request was sent, but we log
    // anything just in case.
    LOG(WARNING) << dummy_response.DebugString();
  }

  EXPECT_EQ(client_stream->Finish().error_code(),
            grpc::StatusCode::UNAVAILABLE);
}

}  // namespace
}  // namespace p4rt_app

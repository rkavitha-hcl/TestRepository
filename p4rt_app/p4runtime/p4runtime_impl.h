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
#ifndef GOOGLE_P4RT_APP_P4RUNTIME_P4RUNTIME_IMPL_H_
#define GOOGLE_P4RT_APP_P4RUNTIME_P4RUNTIME_IMPL_H_

#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "boost/bimap.hpp"
#include "grpcpp/grpcpp.h"
#include "grpcpp/server_context.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_constraints/backend/constraint_info.h"
#include "p4_pdpi/ir.h"
#include "p4rt_app/p4runtime/sdn_controller_manager.h"
#include "p4rt_app/sonic/adapters/consumer_notifier_adapter.h"
#include "p4rt_app/sonic/adapters/db_connector_adapter.h"
#include "p4rt_app/sonic/adapters/producer_state_table_adapter.h"
#include "p4rt_app/sonic/adapters/system_call_adapter.h"
#include "p4rt_app/sonic/app_db_manager.h"
#include "p4rt_app/sonic/packetio_interface.h"
#include "p4rt_app/sonic/packetio_port.h"
#include "p4rt_app/sonic/response_handler.h"
#include "p4rt_app/sonic/vrf_entry_translation.h"
#include "swss/component_state_helper_interface.h"

namespace p4rt_app {

struct P4RuntimeImplOptions {
  bool use_genetlink = false;
  bool translate_port_ids = true;
};

class P4RuntimeImpl : public p4::v1::P4Runtime::Service {
 public:
  // TODO: find way to group arguments so we don't have to pass so
  // many at once.
  P4RuntimeImpl(
      std::unique_ptr<sonic::DBConnectorAdapter> app_db_client,
      std::unique_ptr<sonic::DBConnectorAdapter> app_state_db_client,
      std::unique_ptr<sonic::DBConnectorAdapter> counter_db_client,
      std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_p4rt,
      std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_p4rt,
      std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_vrf,
      std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_vrf,
      std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_hash,
      std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_hash,
      std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_switch,
      std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_switch,
      std::unique_ptr<sonic::PacketIoInterface> packetio_impl,
      swss::ComponentStateHelperInterface& component_state,
      swss::SystemStateHelperInterface& system_state,
      const P4RuntimeImplOptions& p4rt_options);
  ~P4RuntimeImpl() override = default;

  // Determines the type of write request (e.g. table entry, direct counter
  // entry, etc.) then passes work off to a helper method.
  grpc::Status Write(grpc::ServerContext* context,
                     const p4::v1::WriteRequest* request,
                     p4::v1::WriteResponse* response) override
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  grpc::Status Read(
      grpc::ServerContext* context, const p4::v1::ReadRequest* request,
      grpc::ServerWriter<p4::v1::ReadResponse>* response_writer) override
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  grpc::Status SetForwardingPipelineConfig(
      grpc::ServerContext* context,
      const p4::v1::SetForwardingPipelineConfigRequest* request,
      p4::v1::SetForwardingPipelineConfigResponse* response) override
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  grpc::Status GetForwardingPipelineConfig(
      grpc::ServerContext* context,
      const p4::v1::GetForwardingPipelineConfigRequest* request,
      p4::v1::GetForwardingPipelineConfigResponse* response) override
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  grpc::Status StreamChannel(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<p4::v1::StreamMessageResponse,
                               p4::v1::StreamMessageRequest>* stream) override
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  // Will add a new port translation for P4Runtime requests. Duplicate Name/ID
  // pairs will be treated as a no-op, but re-use and empty values will be
  // rejected.
  virtual absl::Status AddPortTranslation(const std::string& port_name,
                                          const std::string& port_id)
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  // Will remove an existing port translation. If the translation does not exist
  // it is treated as a no-op and quitely passes. However, the port name cannot
  // be an empty string.
  virtual absl::Status RemovePortTranslation(const std::string& port_name)
      ABSL_LOCKS_EXCLUDED(server_state_lock_);

  // Verifies state for the P4RT App. These are checks like:
  //  * Do P4RT table entries match in AppStateDb and AppDb.
  //  * Do VRF_TABLE entries match in AppStateDb and AppDb.
  //  * Do HASH_TABLE entries match in AppStateDb and AppDb.
  //  * Do SWITCH_TABLE entries match in AppStateDb and AppDb.
  //
  // NOTE: We do not verify ownership of table entries today. Therefore, shared
  // tables (e.g. VRF_TABLE) could cause false positives.
  virtual absl::Status VerifyState() ABSL_LOCKS_EXCLUDED(server_state_lock_);

 protected:
  // Simple constructor that should only be used for testing purposes.
  P4RuntimeImpl(swss::ComponentStateHelperInterface& component_state,
                swss::SystemStateHelperInterface& system_state,
                bool translate_port_ids)
      : component_state_(component_state),
        system_state_(system_state),
        translate_port_ids_(translate_port_ids) {}

 private:
  P4RuntimeImpl(const P4RuntimeImpl&) = delete;
  P4RuntimeImpl& operator=(const P4RuntimeImpl&) = delete;

  // Get and process response from the notification channel,
  // if on error, restore the APPL_DB to the last good state.
  // Uses, the key of the inserted entry to match the response
  // and restore if needed.
  pdpi::IrUpdateStatus GetAndProcessResponse(absl::string_view key);

  absl::Status HandlePacketOutRequest(const p4::v1::PacketOut& packet_out)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(server_state_lock_);

  absl::Status ApplyForwardingPipelineConfig(const pdpi::IrP4Info& ir_p4info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(server_state_lock_);

  // Defines the callback lambda function to be invoked for receive packets
  // and calls into the sonic::StartReceive to spawn the receiver thread.
  ABSL_MUST_USE_RESULT absl::StatusOr<std::thread> StartReceive(
      bool use_genetlink);

  // Mutex for constraining actions to access and modify server state.
  absl::Mutex server_state_lock_;

  // A RedisDB interface to handle requests into AppDb tables that cannot be
  // done through the ProducerStateTable interface. For example, read out all
  // P4RT entries.
  std::unique_ptr<sonic::DBConnectorAdapter> app_db_client_
      ABSL_GUARDED_BY(server_state_lock_);

  // A RedisDB interface to handle requests into the AppStateDb tables that
  // cannot be done through other interfaces.
  std::unique_ptr<sonic::DBConnectorAdapter> app_state_db_client_
      ABSL_GUARDED_BY(server_state_lock_);

  // A RedisDB interface to handle requests into the CounterDb tables that
  // cannot be done through other interfaces.
  std::unique_ptr<sonic::DBConnectorAdapter> counter_db_client_
      ABSL_GUARDED_BY(server_state_lock_);

  // A RedisDB interface to write entries into the P4RT AppDb table.
  std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_p4rt_
      ABSL_GUARDED_BY(server_state_lock_);
  std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_p4rt_
      ABSL_GUARDED_BY(server_state_lock_);

  // A RedisDB interface to write entries into the VRF_TABLE AppDb table.
  std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_vrf_
      ABSL_GUARDED_BY(server_state_lock_);
  std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_vrf_
      ABSL_GUARDED_BY(server_state_lock_);

  // A RedisDB interface to write entries into the HASH_TABLE AppDb table.
  std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_hash_
      ABSL_GUARDED_BY(server_state_lock_);
  std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_hash_
      ABSL_GUARDED_BY(server_state_lock_);

  // A RedisDB interface to write entries into the SWITCH_TABLE AppDb table.
  std::unique_ptr<sonic::ProducerStateTableAdapter> app_db_table_switch_
      ABSL_GUARDED_BY(server_state_lock_);
  std::unique_ptr<sonic::ConsumerNotifierAdapter> app_db_notifier_switch_
      ABSL_GUARDED_BY(server_state_lock_);

  // P4RT can accept multiple connections to a single switch for redundancy.
  // When there is >1 connection the switch chooses a primary which is used for
  // PacketIO, and is the only connection allowed to write updates.
  //
  // It is possible for connections to be made for specific roles. In which case
  // one primary connection is allowed for each distinct role.
  std::unique_ptr<SdnControllerManager> controller_manager_
      ABSL_GUARDED_BY(server_state_lock_);

  // SONiC uses name to reference ports (e.g. Ethernet4), but the controller can
  // be configured to send port IDs. The P4RT App takes responsibility for
  // translating between the two.
  //
  // boost::bimap<SONiC port name, controller ID>;
  boost::bimap<std::string, std::string> port_translation_map_
      ABSL_GUARDED_BY(server_state_lock_);

  // A forwarding pipeline config with a P4Info protobuf will be set once a
  // controller connects to the swtich. Only after we receive this config can
  // the P4RT service start processing write requests.
  absl::optional<p4::v1::ForwardingPipelineConfig> forwarding_pipeline_config_
      ABSL_GUARDED_BY(server_state_lock_);

  // Once we receive the P4Info we create a pdpi::IrP4Info object which allows
  // us to translate the PI requests into human-readable objects.
  absl::optional<pdpi::IrP4Info> ir_p4info_ ABSL_GUARDED_BY(server_state_lock_);

  // The P4Info can use annotations to specify table constraints for specific
  // tables. The P4RT service will reject any table entry requests that do not
  // meet these constraints.
  absl::optional<p4_constraints::ConstraintInfo> p4_constraint_info_;

  // PacketIoImplementation object.
  std::thread receive_thread_;
  std::unique_ptr<sonic::PacketIoInterface> packetio_impl_
      ABSL_GUARDED_BY(server_state_lock_);

  // When the switch is in critical state the P4RT service shuould not accept
  // write requests, but can still handle reads.
  swss::ComponentStateHelperInterface& component_state_;
  swss::SystemStateHelperInterface& system_state_;

  // Some switch enviornments cannot rely on the SONiC port names, and can
  // instead choose to use port ID's configured through gNMI.
  const bool translate_port_ids_;
};

}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_P4RUNTIME_P4RUNTIME_IMPL_H_

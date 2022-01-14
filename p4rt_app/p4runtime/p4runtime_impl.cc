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
#include "p4rt_app/p4runtime/p4runtime_impl.h"

#include <endian.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/substitute.h"
#include "boost/bimap.hpp"
#include "glog/logging.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/rpc/code.pb.h"
#include "grpcpp/impl/codegen/status.h"
#include "grpcpp/support/status.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_constraints/backend/constraint_info.h"
#include "p4_constraints/backend/interpreter.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/utils/annotation_parser.h"
#include "p4_pdpi/utils/ir.h"
#include "p4rt_app/p4runtime/ir_translation.h"
#include "p4rt_app/p4runtime/p4info_verification.h"
#include "p4rt_app/p4runtime/packetio_helpers.h"
#include "p4rt_app/sonic/adapters/consumer_notifier_adapter.h"
#include "p4rt_app/sonic/adapters/db_connector_adapter.h"
#include "p4rt_app/sonic/adapters/producer_state_table_adapter.h"
#include "p4rt_app/sonic/app_db_acl_def_table_manager.h"
#include "p4rt_app/sonic/app_db_manager.h"
#include "p4rt_app/sonic/hashing.h"
#include "p4rt_app/sonic/packetio_impl.h"
#include "p4rt_app/sonic/packetio_interface.h"
#include "p4rt_app/sonic/packetio_port.h"
#include "p4rt_app/sonic/response_handler.h"
#include "p4rt_app/sonic/state_verification.h"
#include "p4rt_app/sonic/vrf_entry_translation.h"
#include "p4rt_app/utils/status_utility.h"
#include "p4rt_app/utils/table_utility.h"
#include "sai_p4/fixed/ids.h"
#include "sai_p4/fixed/roles.h"
#include "swss/component_state_helper_interface.h"

namespace p4rt_app {
namespace {

grpc::Status EnterCriticalState(
    const std::string& message,
    swss::ComponentStateHelperInterface& state_helper) {
  LOG(ERROR) << "Entering critical state: " << message;
  state_helper.ReportComponentState(swss::ComponentState::kError, message);
  return grpc::Status(grpc::StatusCode::INTERNAL,
                      absl::StrCat("[P4RT App going CRITICAL] ", message));
}

absl::Status SupportedTableEntryRequest(const p4::v1::TableEntry& table_entry) {
  if (table_entry.table_id() != 0 || !table_entry.match().empty() ||
      table_entry.priority() != 0 || !table_entry.metadata().empty() ||
      table_entry.has_action() || table_entry.is_default_action() != false) {
    return gutil::UnimplementedErrorBuilder()
           << "Read request for table entry: "
           << table_entry.ShortDebugString();
  }
  return absl::OkStatus();
}

absl::Status AllowRoleAccessToTable(const std::string& role_name,
                                    const std::string& table_name,
                                    const pdpi::IrP4Info& p4_info) {
  // The defulat role can access any table.
  if (role_name.empty()) return absl::OkStatus();

  auto table_def = p4_info.tables_by_name().find(table_name);
  if (table_def == p4_info.tables_by_name().end()) {
    return gutil::InternalErrorBuilder()
           << "Could not find table '" << table_name
           << "' when checking role access. Did an IR translation fail "
              "somewhere?";
  }

  if (table_def->second.role() != role_name) {
    return gutil::PermissionDeniedErrorBuilder()
           << "Role '" << role_name << "' is not allowd access to table '"
           << table_name << "'.";
  }

  return absl::OkStatus();
}

sonic::AppDbTableType GetAppDbTableType(
    const pdpi::IrTableEntry& ir_table_entry) {
  if (ir_table_entry.table_name() == "vrf_table") {
    return sonic::AppDbTableType::VRF_TABLE;
  }

  // By default we assume and AppDb P4RT entry.
  return sonic::AppDbTableType::P4RT;
}

// Read P4Runtime table entries out of the AppStateDb, and append them to the
// read response.
absl::Status AppendTableEntryReads(
    p4::v1::ReadResponse& response, const p4::v1::TableEntry& pi_table_entry,
    const pdpi::IrP4Info& p4_info, const std::string& role_name,
    bool translate_port_ids,
    const boost::bimap<std::string, std::string>& port_translation_map,
    sonic::DBConnectorAdapter& app_state_db_client,
    sonic::DBConnectorAdapter& counters_db_client) {
  RETURN_IF_ERROR(SupportedTableEntryRequest(pi_table_entry));

  // Get all P4RT keys from the AppDb.
  for (const auto& app_db_key :
       sonic::GetAllAppDbP4TableEntryKeys(app_state_db_client)) {
    // Read a single table entry out of the AppDb
    ASSIGN_OR_RETURN(
        pdpi::IrTableEntry ir_table_entry,
        sonic::ReadAppDbP4TableEntry(p4_info, app_state_db_client,
                                     counters_db_client, app_db_key));

    // Only attach the entry if the role expects it.
    auto allow_access =
        AllowRoleAccessToTable(role_name, ir_table_entry.table_name(), p4_info);
    if (!allow_access.ok()) {
      VLOG(2) << "Ignoring read: " << allow_access;
      continue;
    }

    RETURN_IF_ERROR(TranslateTableEntry(
        TranslateTableEntryOptions{
            .direction = TranslationDirection::kForController,
            .ir_p4_info = p4_info,
            .translate_port_ids = translate_port_ids,
            .port_map = port_translation_map},
        ir_table_entry));

    auto translate_status = pdpi::IrTableEntryToPi(p4_info, ir_table_entry);
    if (!translate_status.ok()) {
      LOG(ERROR) << "PDPI could not translate IR table entry to PI: "
                 << ir_table_entry.DebugString();
      return gutil::StatusBuilder(translate_status.status().code())
             << "[P4RT/PDPI] " << translate_status.status().message();
    }
    *response.add_entities()->mutable_table_entry() = *translate_status;
  }

  // Get all VRF_TABLE entries from the AppDb.
  ASSIGN_OR_RETURN(std::vector<pdpi::IrTableEntry> vrf_entries,
                   sonic::GetAllAppDbVrfTableEntries(app_state_db_client));
  for (const auto& ir_table_entry : vrf_entries) {
    auto translate_status = pdpi::IrTableEntryToPi(p4_info, ir_table_entry);
    if (!translate_status.ok()) {
      LOG(ERROR) << "PDPI could not translate IR table entry to PI: "
                 << ir_table_entry.DebugString();
      return gutil::StatusBuilder(translate_status.status().code())
             << "[P4RT/PDPI] " << translate_status.status().message();
    }
    *response.add_entities()->mutable_table_entry() = *translate_status;
  }
  return absl::OkStatus();
}

absl::StatusOr<p4::v1::ReadResponse> DoRead(
    const p4::v1::ReadRequest& request, const pdpi::IrP4Info p4_info,
    bool translate_port_ids,
    const boost::bimap<std::string, std::string>& port_translation_map,
    sonic::DBConnectorAdapter& app_state_db_client,
    sonic::DBConnectorAdapter& counters_db_client) {
  p4::v1::ReadResponse response;
  for (const auto& entity : request.entities()) {
    LOG(INFO) << "Read request: " << entity.ShortDebugString();
    switch (entity.entity_case()) {
      case p4::v1::Entity::kTableEntry: {
        RETURN_IF_ERROR(AppendTableEntryReads(
            response, entity.table_entry(), p4_info, request.role(),
            translate_port_ids, port_translation_map, app_state_db_client,
            counters_db_client));
        break;
      }
      default:
        return gutil::UnimplementedErrorBuilder()
               << "Read has not been implemented for: "
               << entity.ShortDebugString();
    }
  }
  return response;
}

// Generates a StreamMessageResponse error based on an absl::Status.
p4::v1::StreamMessageResponse GenerateErrorResponse(absl::Status status) {
  grpc::Status grpc_status = gutil::AbslStatusToGrpcStatus(status);
  p4::v1::StreamMessageResponse response;
  auto error = response.mutable_error();
  error->set_canonical_code(grpc_status.error_code());
  error->set_message(grpc_status.error_message());
  return response;
}

// Generates StreamMessageResponse with errors for PacketIO
p4::v1::StreamMessageResponse GenerateErrorResponse(
    absl::Status status, const p4::v1::PacketOut& packet) {
  p4::v1::StreamMessageResponse response = GenerateErrorResponse(status);
  *response.mutable_error()->mutable_packet_out()->mutable_packet_out() =
      packet;
  return response;
}

// Compares two P4Info protobufs and returns true if they represent the
// same information. Differences are reported in the optional string.
bool P4InfoEquals(const p4::config::v1::P4Info& left,
                  const p4::config::v1::P4Info& right,
                  std::string* diff_report) {
  google::protobuf::util::MessageDifferencer differencer;
  differencer.set_repeated_field_comparison(
      google::protobuf::util::MessageDifferencer::AS_SMART_SET);
  differencer.set_report_matches(false);
  differencer.set_report_moves(false);
  if (diff_report != nullptr) {
    differencer.ReportDifferencesToString(diff_report);
  }
  return differencer.Compare(left, right);
}

absl::StatusOr<pdpi::IrTableEntry> DoPiTableEntryToIr(
    const p4::v1::TableEntry& pi_table_entry, const pdpi::IrP4Info& p4_info,
    const std::string& role_name, bool translate_port_ids,
    const boost::bimap<std::string, std::string>& port_translation_map,
    bool translate_key_only) {
  auto translate_status =
      pdpi::PiTableEntryToIr(p4_info, pi_table_entry, translate_key_only);
  if (!translate_status.ok()) {
    LOG(WARNING) << "PDPI could not translate PI table entry to IR: "
                 << pi_table_entry.DebugString();
    return gutil::StatusBuilder(translate_status.status().code())
           << "[P4RT/PDPI] " << translate_status.status().message();
  }
  pdpi::IrTableEntry ir_table_entry = *translate_status;

  // Verify the table entry can be written to the table.
  RETURN_IF_ERROR(
      AllowRoleAccessToTable(role_name, ir_table_entry.table_name(), p4_info));

  RETURN_IF_ERROR(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = p4_info,
          .translate_port_ids = translate_port_ids,
          .port_map = port_translation_map},
      ir_table_entry));
  return ir_table_entry;
}

sonic::AppDbUpdates PiTableEntryUpdatesToIr(
    const p4::v1::WriteRequest& request, const pdpi::IrP4Info& p4_info,
    const p4_constraints::ConstraintInfo& constraint_info,
    bool translate_port_ids,
    const boost::bimap<std::string, std::string>& port_translation_map,
    pdpi::IrWriteResponse* response) {
  sonic::AppDbUpdates ir_updates;
  for (const auto& update : request.updates()) {
    // An RPC response should be created for every updater.
    auto entry_status = response->add_statuses();
    ++ir_updates.total_rpc_updates;

    // If the constraints are not met then we should just report an error (i.e.
    // do not try to handle the entry in lower layers).
    absl::StatusOr<bool> meets_constraint =
        p4_constraints::EntryMeetsConstraint(update.entity().table_entry(),
                                             constraint_info);
    if (!meets_constraint.ok()) {
      // A status failure implies that the TableEntry was not formatted
      // correctly. So we could not check the constraints.
      LOG(WARNING) << "Could not verify P4 constraint: "
                   << update.entity().table_entry().DebugString();
      *entry_status = GetIrUpdateStatus(meets_constraint.status());
      continue;
    }
    if (*meets_constraint == false) {
      // A false result implies the constraints were not met.
      LOG(WARNING) << "Entry does not meet P4 constraint: "
                   << update.entity().table_entry().DebugString();
      *entry_status = GetIrUpdateStatus(
          gutil::InvalidArgumentErrorBuilder()
          << "Does not meet constraints required for the table entry.");
      continue;
    }

    // If we cannot translate it then we should just report an error (i.e. do
    // not try to handle it in lower layers). When doing a DELETE, translate
    // only the key part of the table entry because, from the specs, the control
    // plane is not required to send the full entry.
    auto ir_table_entry = DoPiTableEntryToIr(
        update.entity().table_entry(), p4_info, request.role(),
        translate_port_ids, port_translation_map,
        update.type() == p4::v1::Update::DELETE);
    *entry_status = GetIrUpdateStatus(ir_table_entry.status());
    if (!ir_table_entry.ok()) {
      LOG(WARNING) << "Could not translate PI to IR: "
                   << update.entity().table_entry().DebugString();
      continue;
    }

    int rpc_index = response->statuses_size() - 1;
    ir_updates.entries.push_back(sonic::AppDbEntry{
        .rpc_index = rpc_index,
        .entry = *ir_table_entry,
        .update_type = update.type(),
        .appdb_table = GetAppDbTableType(*ir_table_entry),
    });
  }
  return ir_updates;
}

}  // namespace

P4RuntimeImpl::P4RuntimeImpl(
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
    const P4RuntimeImplOptions& p4rt_options)
    : app_db_client_(std::move(app_db_client)),
      app_state_db_client_(std::move(app_state_db_client)),
      counter_db_client_(std::move(counter_db_client)),
      app_db_table_p4rt_(std::move(app_db_table_p4rt)),
      app_db_notifier_p4rt_(std::move(app_db_notifier_p4rt)),
      app_db_table_vrf_(std::move(app_db_table_vrf)),
      app_db_notifier_vrf_(std::move(app_db_notifier_vrf)),
      app_db_table_hash_(std::move(app_db_table_hash)),
      app_db_notifier_hash_(std::move(app_db_notifier_hash)),
      app_db_table_switch_(std::move(app_db_table_switch)),
      app_db_notifier_switch_(std::move(app_db_notifier_switch)),
      packetio_impl_(std::move(packetio_impl)),
      component_state_(component_state),
      system_state_(system_state),
      translate_port_ids_(p4rt_options.translate_port_ids) {
  absl::optional<std::string> init_failure;

  // Start the controller manager.
  controller_manager_ = absl::make_unique<SdnControllerManager>();

  // Spawn the receiver thread to receive In packets.
  auto status_or = StartReceive(p4rt_options.use_genetlink);
  if (status_or.ok()) {
    receive_thread_ = std::move(*status_or);
  } else {
    init_failure = absl::StrCat("Failed to spawn Receive thread, error: ",
                                status_or.status().ToString());
  }

  // If we have an initialization issue then immediatly go critical, otherwise
  // report to global state that P4RT is up.
  if (init_failure.has_value()) {
    component_state_.ReportComponentState(swss::ComponentState::kError,
                                          *init_failure);
  } else {
    component_state_.ReportComponentState(swss::ComponentState::kUp,
                                          /*reason=*/"");
  }
}

grpc::Status P4RuntimeImpl::Write(grpc::ServerContext* context,
                                  const p4::v1::WriteRequest* request,
                                  p4::v1::WriteResponse* response) {
#ifdef __EXCEPTIONS
  try {
#endif
    absl::MutexLock l(&server_state_lock_);

    // Verify the request comes from the primary connection.
    auto connection_status = controller_manager_->AllowRequest(*request);
    if (!connection_status.ok()) {
      return connection_status;
    }

    // Reject any write request if the switch is in a CRITICAL state.
    if (system_state_.IsSystemCritical()) {
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          system_state_.GetSystemCriticalReason());
    }

    // We can only program the flow if the forwarding pipeline has been set.
    if (!ir_p4info_.has_value()) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Switch has not configured the forwarding pipeline.");
    }

    pdpi::IrWriteRpcStatus rpc_status;
    pdpi::IrWriteResponse* rpc_response = rpc_status.mutable_rpc_response();
    sonic::AppDbUpdates app_db_updates = PiTableEntryUpdatesToIr(
        *request, *ir_p4info_, *p4_constraint_info_, translate_port_ids_,
        port_translation_map_, rpc_response);

    // Any AppDb update failures should be appended to the `rpc_response`. If
    // UpdateAppDb fails we should go critical.
    auto app_db_write_status = sonic::UpdateAppDb(
        app_db_updates, *ir_p4info_, *app_db_table_p4rt_,
        *app_db_notifier_p4rt_, *app_db_client_, *app_state_db_client_,
        *app_db_table_vrf_, *app_db_notifier_vrf_, rpc_response);
    if (!app_db_write_status.ok()) {
      return EnterCriticalState(
          absl::StrCat("Unexpected error calling UpdateAppDb: ",
                       app_db_write_status.ToString()),
          component_state_);
    }

    auto grpc_status = pdpi::IrWriteRpcStatusToGrpcStatus(rpc_status);
    if (!grpc_status.ok()) {
      LOG(ERROR) << "PDPI failed to translate RPC status to gRPC status: "
                 << rpc_status.DebugString();
      return EnterCriticalState(grpc_status.status().ToString(),
                                component_state_);
    }
    return *grpc_status;
#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    return EnterCriticalState(
        absl::StrCat("Exception caught in ", __func__, ", error:", e.what()),
        component_state_);
  } catch (...) {
    return EnterCriticalState(
        absl::StrCat("Unknown exception caught in ", __func__, "."),
        component_state_);
  }
#endif
}

grpc::Status P4RuntimeImpl::Read(
    grpc::ServerContext* context, const p4::v1::ReadRequest* request,
    grpc::ServerWriter<p4::v1::ReadResponse>* response_writer) {
  absl::MutexLock l(&server_state_lock_);
#ifdef __EXCEPTIONS
  try {
#endif
    if (!ir_p4info_.has_value()) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Switch has no ForwardingPipelineConfig.");
    }
    if (request == nullptr) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "ReadRequest cannot be a nullptr.");
    }
    if (response_writer == nullptr) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "ReadResponse writer cannot be a nullptr.");
    }

    auto response_status = DoRead(*request, ir_p4info_.value(),
                                  translate_port_ids_, port_translation_map_,
                                  *app_state_db_client_, *counter_db_client_);
    if (!response_status.ok()) {
      LOG(WARNING) << "Read failure: " << response_status.status();
      return grpc::Status(
          grpc::StatusCode::UNKNOWN,
          absl::StrCat("Read failure: ", response_status.status().ToString()));
    }

    response_writer->Write(response_status.value());
    return grpc::Status::OK;
#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    return EnterCriticalState(
        absl::StrCat("Exception caught in ", __func__, ", error:", e.what()),
        component_state_);
  } catch (...) {
    return EnterCriticalState(
        absl::StrCat("Unknown exception caught in ", __func__, "."),
        component_state_);
  }
#endif
}

grpc::Status P4RuntimeImpl::StreamChannel(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<p4::v1::StreamMessageResponse,
                             p4::v1::StreamMessageRequest>* stream) {
#ifdef __EXCEPTIONS
  try {
#endif
    if (context == nullptr) {
      LOG(WARNING) << "StreamChannel context is a nullptr.";
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Context cannot be nullptr.");
    }

    // We create a unique SDN connection object for every active connection.
    auto sdn_connection = absl::make_unique<SdnConnection>(context, stream);
    LOG(INFO) << "StreamChannel is open with peer '" << context->peer() << "'.";

    // While the connection is active we can receive and send requests.
    p4::v1::StreamMessageRequest request;
    while (stream->Read(&request)) {
      absl::MutexLock l(&server_state_lock_);

      switch (request.update_case()) {
        case p4::v1::StreamMessageRequest::kArbitration: {
          LOG(INFO) << "Received arbitration request from '" << context->peer()
                    << "': " << request.ShortDebugString();

          auto status = controller_manager_->HandleArbitrationUpdate(
              request.arbitration(), sdn_connection.get());
          if (!status.ok()) {
            LOG(WARNING) << "Failed arbitration request for '"
                         << context->peer() << "': " << status.error_message();
            controller_manager_->Disconnect(sdn_connection.get());
            return status;
          }
          break;
        }
        case p4::v1::StreamMessageRequest::kPacket: {
          if (controller_manager_
                  ->AllowRequest(sdn_connection->GetRoleName(),
                                 sdn_connection->GetElectionId())
                  .ok()) {
            // If we're the primary connection we can try to handle the
            // PacketOut request.
            absl::Status packet_out_status =
                HandlePacketOutRequest(request.packet());
            if (!packet_out_status.ok()) {
              LOG(WARNING) << "Could not handle PacketOut request: "
                           << packet_out_status;
              sdn_connection->SendStreamMessageResponse(
                  GenerateErrorResponse(packet_out_status, request.packet()));
            }
          } else {
            // Otherwise, if it's not the primary connection trying to send a
            // message so we return a PERMISSION_DENIED error.
            LOG(WARNING) << "Non-primary controller '" << context->peer()
                         << "' is trying to send PacketOut requests.";
            sdn_connection->SendStreamMessageResponse(
                GenerateErrorResponse(gutil::PermissionDeniedErrorBuilder()
                                          << "Only the primary connection can "
                                             "send PacketOut requests.",
                                      request.packet()));
          }
          break;
        }
        default:
          LOG(WARNING) << "Stream Channel '" << context->peer()
                       << "' has sent a request that was unhandled: "
                       << request.DebugString();
          sdn_connection->SendStreamMessageResponse(
              GenerateErrorResponse(gutil::UnimplementedErrorBuilder()
                                    << "Stream update type is not supported."));
      }
    }

    // Disconnect the controller from the list of available connections, and
    // inform any other connections about arbitration changes.
    {
      absl::MutexLock l(&server_state_lock_);
      controller_manager_->Disconnect(sdn_connection.get());
    }

    LOG(INFO) << "Closing stream to peer '" << context->peer() << "'.";
    if (context->IsCancelled()) {
      LOG(WARNING)
          << "Stream was canceled and the peer may not have been informed.";
    }
    return grpc::Status::OK;
#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    return EnterCriticalState(
        absl::StrCat("Exception caught in ", __func__, ", error:", e.what()),
        component_state_);
  } catch (...) {
    return EnterCriticalState(
        absl::StrCat("Unknown exception caught in ", __func__, "."),
        component_state_);
  }
#endif
}

grpc::Status P4RuntimeImpl::SetForwardingPipelineConfig(
    grpc::ServerContext* context,
    const p4::v1::SetForwardingPipelineConfigRequest* request,
    p4::v1::SetForwardingPipelineConfigResponse* response) {
#ifdef __EXCEPTIONS
  try {
#endif
    absl::MutexLock l(&server_state_lock_);
    LOG(INFO)
        << "Received SetForwardingPipelineConfig request from election id: "
        << request->election_id().ShortDebugString();

    // Verify this connection is allowed to set the P4Info.
    auto connection_status = controller_manager_->AllowRequest(*request);
    if (!connection_status.ok()) {
      return connection_status;
    }

    // The pipeline cannot be changed if the switch is in a CRITICAL state.
    if (system_state_.IsSystemCritical()) {
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          system_state_.GetSystemCriticalReason());
    }

    // P4Runtime allows for the controller to configure the switch in multiple
    // ways. The expectations are outlined here:
    //
    // https://p4.org/p4-spec/p4runtime/main/P4Runtime-Spec.html#sec-setforwardingpipelineconfig-rpc
    grpc::Status action_status;
    VLOG(1) << "Request action: " << request->Action_Name(request->action());
    switch (request->action()) {
      case p4::v1::SetForwardingPipelineConfigRequest::VERIFY:
        action_status = VerifyPipelineConfig(*request);
        break;
      case p4::v1::SetForwardingPipelineConfigRequest::VERIFY_AND_COMMIT:
        action_status = VerifyAndCommitPipelineConfig(*request);
        break;
      case p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT: {
        action_status = ReconcileAndCommitPipelineConfig(*request);
        break;
      }
      default: {
        LOG(WARNING) << "Received SetForwardingPipelineConfigRequest with an "
                        "unsupported action: "
                     << request->Action_Name(request->action());
        return grpc::Status(
            grpc::StatusCode::UNIMPLEMENTED,
            absl::StrFormat(
                "SetForwardingPipelineConfig action '%s' is unsupported.",
                request->Action_Name(request->action())));
      }
    }

    if (action_status.error_code() == grpc::StatusCode::INTERNAL) {
      LOG(ERROR) << "Critically failed to apply ForwardingPipelineConfig: "
                 << action_status.error_message();
      return EnterCriticalState(action_status.error_message(),
                                component_state_);
    } else if (!action_status.ok()) {
      LOG(WARNING) << "SetForwardingPipelineConfig failed: "
                   << action_status.error_message();
      return action_status;
    }

    LOG(INFO) << absl::StreamFormat(
        "SetForwardingPipelineConfig completed '%s' successfully.",
        p4::v1::SetForwardingPipelineConfigRequest::Action_Name(
            request->action()));

#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    return EnterCriticalState(
        absl::StrCat("Exception caught in ", __func__, ", error:", e.what()),
        component_state_);
  } catch (...) {
    return EnterCriticalState(
        absl::StrCat("Unknown exception caught in ", __func__, "."),
        component_state_);
  }
#endif

  return grpc::Status::OK;
}

grpc::Status P4RuntimeImpl::GetForwardingPipelineConfig(
    grpc::ServerContext* context,
    const p4::v1::GetForwardingPipelineConfigRequest* request,
    p4::v1::GetForwardingPipelineConfigResponse* response) {
  absl::MutexLock l(&server_state_lock_);
#ifdef __EXCEPTIONS
  try {
#endif
    if (ir_p4info_.has_value()) {
      switch (request->response_type()) {
        case p4::v1::GetForwardingPipelineConfigRequest::COOKIE_ONLY:
          *response->mutable_config()->mutable_cookie() =
              forwarding_pipeline_config_.value().cookie();
          break;
        default:
          *response->mutable_config() = forwarding_pipeline_config_.value();
          break;
      }
    }
    return grpc::Status(grpc::StatusCode::OK, "");
#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    return EnterCriticalState(
        absl::StrCat("Exception caught in ", __func__, ", error:", e.what()),
        component_state_);
  } catch (...) {
    return EnterCriticalState(
        absl::StrCat("Unknown exception caught in ", __func__, "."),
        component_state_);
  }
#endif
}

absl::Status P4RuntimeImpl::AddPortTranslation(const std::string& port_name,
                                               const std::string& port_id) {
  absl::MutexLock l(&server_state_lock_);

  // Do not allow empty strings.
  if (port_name.empty()) {
    return absl::InvalidArgumentError(
        "Cannot add port translation without the port name.");
  } else if (port_id.empty()) {
    return absl::InvalidArgumentError(
        "Cannot add port translation without the port ID.");
  }

  // If the Port Name/ID pair already exists then the operation is a no-op.
  if (const auto iter = port_translation_map_.left.find(port_name);
      iter != port_translation_map_.left.end() && iter->second == port_id) {
    return absl::OkStatus();
  }

  // However, we do not accept reuse of existing values.
  if (const auto& [_, success] =
          port_translation_map_.insert({port_name, port_id});
      !success) {
    return gutil::AlreadyExistsErrorBuilder()
           << "Could not add port '" << port_name << "' with ID '" << port_id
           << "' because an entry already exists.";
  }

  // Add the port to Packet I/O.
  return packetio_impl_->AddPacketIoPort(port_name);
}

absl::Status P4RuntimeImpl::RemovePortTranslation(
    const std::string& port_name) {
  absl::MutexLock l(&server_state_lock_);

  // Do not allow empty strings.
  if (port_name.empty()) {
    return absl::InvalidArgumentError(
        "Cannot remove port translation without the port name.");
  }

  if (auto port = port_translation_map_.left.find(port_name);
      port != port_translation_map_.left.end()) {
    port_translation_map_.left.erase(port);

    // Remove port from Packet I/O.
    RETURN_IF_ERROR(packetio_impl_->RemovePacketIoPort(port_name));
  }

  return absl::OkStatus();
}

absl::Status P4RuntimeImpl::VerifyState() {
  absl::MutexLock l(&server_state_lock_);

  std::vector<std::string> failures = {"P4RT App State Verification failures:"};

  // Verify the P4RT entries.
  std::vector<std::string> p4rt_table_failures =
      sonic::VerifyAppStateDbAndAppDbEntries(
          app_db_table_p4rt_->get_table_name(), *app_state_db_client_,
          *app_db_client_);
  if (!p4rt_table_failures.empty()) {
    failures.insert(failures.end(), p4rt_table_failures.begin(),
                    p4rt_table_failures.end());
  }

  // Verify the VRF_TABLE entries.
  std::vector<std::string> vrf_table_failures =
      sonic::VerifyAppStateDbAndAppDbEntries(
          app_db_table_vrf_->get_table_name(), *app_state_db_client_,
          *app_db_client_);
  if (!vrf_table_failures.empty()) {
    failures.insert(failures.end(), vrf_table_failures.begin(),
                    vrf_table_failures.end());
  }

  // Verify the HASH_TABLE entries.
  std::vector<std::string> hash_table_failures =
      sonic::VerifyAppStateDbAndAppDbEntries(
          app_db_table_hash_->get_table_name(), *app_state_db_client_,
          *app_db_client_);
  if (!hash_table_failures.empty()) {
    failures.insert(failures.end(), hash_table_failures.begin(),
                    hash_table_failures.end());
  }

  // Verify the SWITCH_TABLE entries.
  std::vector<std::string> switch_table_failures =
      sonic::VerifyAppStateDbAndAppDbEntries(
          app_db_table_switch_->get_table_name(), *app_state_db_client_,
          *app_db_client_);
  if (!switch_table_failures.empty()) {
    failures.insert(failures.end(), switch_table_failures.begin(),
                    switch_table_failures.end());
  }

  if (failures.size() > 1) {
    // Reports a MINOR alarm to indicate state verification failure.
    // We do not report CRITICAL alarm here because that will stop further
    // programing.
    component_state_.ReportComponentState(swss::ComponentState::kMinor,
                                          absl::StrJoin(failures, "\n  "));
    return gutil::UnknownErrorBuilder() << absl::StrJoin(failures, "\n  ");
  }
  return absl::OkStatus();
}

absl::Status P4RuntimeImpl::HandlePacketOutRequest(
    const p4::v1::PacketOut& packet_out) {
  if (!ir_p4info_.has_value()) {
    return gutil::FailedPreconditionErrorBuilder()
           << "Switch has not configured the forwarding pipeline.";
  }
  return SendPacketOut(*ir_p4info_, translate_port_ids_, port_translation_map_,
                       packetio_impl_.get(), packet_out);
}

grpc::Status P4RuntimeImpl::VerifyPipelineConfig(
    const p4::v1::SetForwardingPipelineConfigRequest& request) const {
  // In all cases where we need to verify a config the spec requires a config to
  // be set.
  if (!request.has_config()) {
    LOG(WARNING) << "ForwardingPipelineConfig is missing the config field.";
    return grpc::Status(
        grpc::StatusCode::INVALID_ARGUMENT,
        "ForwardingPipelineConfig is missing the config field.");
  }

  absl::Status validate_p4info = ValidateP4Info(request.config().p4info());

  // TODO (b/181241450): Re-enable verification checks before SB400 DVT end.
  if (!validate_p4info.ok()) {
    // return gutil::AbslStatusToGrpcStatus(
    //     gutil::StatusBuilder(validate_p4info.code())
    //     << "P4Info is not valid. Details: " << validate_p4info.message());
    LOG(WARNING) << "P4Info is not valid, but we will still try to apply it: "
                 << validate_p4info;
  }
  return grpc::Status::OK;
}

grpc::Status P4RuntimeImpl::VerifyAndCommitPipelineConfig(
    const p4::v1::SetForwardingPipelineConfigRequest& request) {
  // Today we do not clear any forwarding state so if we detect any we return an
  // UNIMPLEMENTED error.
  if (forwarding_pipeline_config_.has_value()) {
    return grpc::Status(
        grpc::StatusCode::UNIMPLEMENTED,
        "Clearing existing forwarding state is not supported. Try using "
        "RECONCILE_AND_COMMIT instead.");
  }

  // Since we cannot have any state today we can use the same code path from
  // RECONCILE_AND_COMMIT to apply the forwarding config.
  return ReconcileAndCommitPipelineConfig(request);
}

grpc::Status P4RuntimeImpl::ReconcileAndCommitPipelineConfig(
    const p4::v1::SetForwardingPipelineConfigRequest& request) {
  grpc::Status verified = VerifyPipelineConfig(request);
  if (!verified.ok()) return verified;

  // We cannot reconcile any config today so if we see that the new forwarding
  // config is different from the current one we just return an error.
  std::string diff_report;
  if (forwarding_pipeline_config_.has_value() &&
      !P4InfoEquals(forwarding_pipeline_config_->p4info(),
                    request.config().p4info(), &diff_report)) {
    LOG(WARNING) << "Cannot modify P4Info once it has been configured.";
    return grpc::Status(
        grpc::StatusCode::UNIMPLEMENTED,
        absl::StrCat(
            "Modifying a configured forwarding pipeline is not currently "
            "supported. Please reboot the device. Configuration "
            "differences:\n",
            diff_report));
  }

  // If the IrP4Info hasn't been set then we need to configure the lower layers.
  if (!ir_p4info_.has_value()) {
    // Collect any P4RT constraints from the P4Info.
    auto constraint_info =
        p4_constraints::P4ToConstraintInfo(request.config().p4info());
    if (!constraint_info.ok()) {
      LOG(WARNING) << "Could not get constraint info from P4Info: "
                   << constraint_info.status();
      return gutil::AbslStatusToGrpcStatus(
          absl::Status(constraint_info.status().code(),
                       absl::StrCat("[P4 Constraint] ",
                                    constraint_info.status().message())));
    }

    // Convert the P4Info into an IrP4Info.
    auto ir_p4info = pdpi::CreateIrP4Info(request.config().p4info());
    if (!ir_p4info.ok()) {
      LOG(WARNING) << "Could not convert P4Info into IrP4Info: "
                   << ir_p4info.status();
      return gutil::AbslStatusToGrpcStatus(absl::Status(
          ir_p4info.status().code(),
          absl::StrCat("[P4RT/PDPI]", ir_p4info.status().message())));
    }
    TranslateIrP4InfoForOrchAgent(*ir_p4info);

    // Apply a config if we don't currently have one.
    absl::Status config_result = ConfigureAppDbTables(*ir_p4info);
    if (!config_result.ok()) {
      LOG(ERROR) << "Failed to apply ForwardingPipelineConfig: "
                 << config_result;
      // TODO: cleanup P4RT table definitions instead of going
      // critical.
      return grpc::Status(grpc::StatusCode::INTERNAL, config_result.ToString());
    }

    // Update P4RuntimeImpl's state only if we succeed.
    p4_constraint_info_ = *std::move(constraint_info);
    ir_p4info_ = *std::move(ir_p4info);
  }

  // The ForwardingPipelineConfig is still updated incase the cookie value has
  // been changed.
  forwarding_pipeline_config_ = request.config();
  return grpc::Status::OK;
}

absl::Status P4RuntimeImpl::ConfigureAppDbTables(
    const pdpi::IrP4Info& ir_p4info) {
  // Setup definitions for each each P4 ACL table.
  for (const auto& pair : ir_p4info.tables_by_name()) {
    std::string table_name = pair.first;
    pdpi::IrTableDefinition table = pair.second;
    ASSIGN_OR_RETURN(table::Type table_type, GetTableType(table),
                     _ << "Failed to configure table " << table_name << ".");

    // Add ACL table definition to AppDb (if applicable).
    if (table_type == table::Type::kAcl) {
      LOG(INFO) << "Configuring ACL table: " << table_name;
      ASSIGN_OR_RETURN(
          std::string acl_key,
          sonic::InsertAclTableDefinition(*app_db_table_p4rt_, table),
          _ << "Failed to add ACL table definition [" << table_name
            << "] to AppDb.");

      // Wait for OA to confirm it can realize the table updates.
      ASSIGN_OR_RETURN(
          pdpi::IrUpdateStatus status,
          sonic::GetAndProcessResponseNotification(
              app_db_table_p4rt_->get_table_name(), *app_db_notifier_p4rt_,
              *app_db_client_, *app_state_db_client_, acl_key));

      // Any issue with the forwarding config should be sent back to the
      // controller as an INVALID_ARGUMENT.
      if (status.code() != google::rpc::OK) {
        return gutil::InvalidArgumentErrorBuilder() << status.message();
      }
    }
  }

  // Program hash table fields used for ECMP hashing.
  ASSIGN_OR_RETURN(auto hash_fields,
                   sonic::ProgramHashFieldTable(
                       ir_p4info, *app_db_table_hash_, *app_db_notifier_hash_,
                       *app_db_client_, *app_state_db_client_));
  // Program hash algorithm and related fields for ECMP hashing.
  RETURN_IF_ERROR(sonic::ProgramSwitchTable(
      ir_p4info, hash_fields, *app_db_table_switch_, *app_db_notifier_switch_,
      *app_db_client_, *app_state_db_client_));
  return absl::OkStatus();
}

absl::StatusOr<std::thread> P4RuntimeImpl::StartReceive(
    const bool use_genetlink) {
  // Define the lambda function for the callback to be executed for every
  // receive packet.
  auto SendPacketInToController =
      [this](const std::string& source_port_name,
             const std::string& target_port_name,
             const std::string& payload) -> absl::Status {
    absl::MutexLock l(&server_state_lock_);

    // Convert Sonic port name to controller port number.
    std::string source_port_id;
    if (translate_port_ids_) {
      ASSIGN_OR_RETURN(source_port_id,
                       TranslatePort(TranslationDirection::kForController,
                                     port_translation_map_, source_port_name),
                       _.SetCode(absl::StatusCode::kInternal).LogError()
                           << "Failed to parse source port");
    } else {
      source_port_id = source_port_name;
    }

    // TODO: Until string port names are supported, re-assign empty
    // target egress port names to match the ingress port.
    std::string target_port_id = source_port_id;
    if (!target_port_name.empty()) {
      if (translate_port_ids_) {
        ASSIGN_OR_RETURN(target_port_id,
                         TranslatePort(TranslationDirection::kForController,
                                       port_translation_map_, target_port_name),
                         _.SetCode(absl::StatusCode::kInternal).LogError()
                             << "Failed to parse target port");
      } else {
        target_port_id = target_port_name;
      }
    }

    // Form the PacketIn metadata fields before writing into the
    // stream.
    ASSIGN_OR_RETURN(auto packet_in,
                     CreatePacketInMessage(source_port_id, target_port_id));
    p4::v1::StreamMessageResponse response;
    *response.mutable_packet() = packet_in;
    *response.mutable_packet()->mutable_payload() = payload;
    // Get the primary streamchannel and write into the stream.
    return controller_manager_->SendStreamMessageToPrimary(
        P4RUNTIME_ROLE_SDN_CONTROLLER, response);
  };

  absl::MutexLock l(&server_state_lock_);
  if (packetio_impl_ == nullptr) {
    return absl::InvalidArgumentError("PacketIoImpl is a required object");
  }

  // Spawn the receiver thread.
  return packetio_impl_->StartReceive(SendPacketInToController, use_genetlink);
}

}  // namespace p4rt_app

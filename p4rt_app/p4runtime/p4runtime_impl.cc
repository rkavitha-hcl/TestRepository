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
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/util/message_differencer.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "p4_pdpi/utils/ir.h"
#include "p4rt_app/p4runtime/p4info_verification.h"
#include "p4rt_app/p4runtime/port_translation.h"
#include "p4rt_app/sonic/app_db_acl_def_table_manager.h"
#include "p4rt_app/sonic/packetio_port.h"
#include "p4rt_app/sonic/response_handler.h"
#include "p4rt_app/utils/status_utility.h"
#include "p4rt_app/utils/table_utility.h"

namespace p4rt_app {

using ::google::protobuf::util::MessageDifferencer;

namespace {

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

// Read P4Runtime table entries out of the AppDb, and append them to the read
// response.
absl::Status AppendTableEntryReads(
    p4::v1::ReadResponse& response, const p4::v1::TableEntry& pi_table_entry,
    const pdpi::IrP4Info& p4_info,
    const boost::bimap<std::string, std::string>& port_translation_map,
    swss::DBConnectorInterface& redis_client, P4RuntimeTweaks* tweak) {
  RETURN_IF_ERROR(SupportedTableEntryRequest(pi_table_entry));

  // Get all P4RT keys from the AppDb.
  for (const auto& app_db_key :
       sonic::GetAllAppDbP4TableEntryKeys(redis_client)) {
    // Read a single table entry out of the AppDb
    ASSIGN_OR_RETURN(
        auto ir_table_entry,
        sonic::ReadAppDbP4TableEntry(p4_info, redis_client, app_db_key));
    // TODO: This failure should put the switch into critical
    // state.
    ASSIGN_OR_RETURN(pdpi::IrTableEntry tweaked_entry,
                     tweak->ForController(ir_table_entry),
                     _ << "Failed to tweak IrTableEntry: "
                       << ir_table_entry.ShortDebugString());
    RETURN_IF_ERROR(
        TranslatePortIdAndNames(PortTranslationDirection::kForController,
                                port_translation_map, tweaked_entry));
    ASSIGN_OR_RETURN(
        *response.add_entities()->mutable_table_entry(),
        pdpi::IrTableEntryToPi(p4_info, tweaked_entry),
        _ << "Original IrTableEntry: " << ir_table_entry.ShortDebugString()
          << "Tweaked IrTableEntry: " << tweaked_entry.ShortDebugString());
  }
  return absl::OkStatus();
}

absl::StatusOr<p4::v1::ReadResponse> DoRead(
    const p4::v1::ReadRequest& request, const pdpi::IrP4Info p4_info,
    const boost::bimap<std::string, std::string>& port_translation_map,
    swss::DBConnectorInterface& redis_client, P4RuntimeTweaks* tweak) {
  p4::v1::ReadResponse response;
  for (const auto& entity : request.entities()) {
    LOG(INFO) << "Read request: " << entity.ShortDebugString();
    switch (entity.entity_case()) {
      case p4::v1::Entity::kTableEntry: {
        RETURN_IF_ERROR(AppendTableEntryReads(response, entity.table_entry(),
                                              p4_info, port_translation_map,
                                              redis_client, tweak))
            << entity.ShortDebugString();
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

// Compares two pdpi::IrP4Info protobufs and returns true if they represent the
// same information. Differences are reported in the optional string.
bool IrP4InfoEquals(const pdpi::IrP4Info& left, const pdpi::IrP4Info& right,
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
    const boost::bimap<std::string, std::string>& port_translation_map) {
  ASSIGN_OR_RETURN(pdpi::IrTableEntry ir_table_entry,
                   pdpi::PiTableEntryToIr(p4_info, pi_table_entry));
  RETURN_IF_ERROR(
      TranslatePortIdAndNames(PortTranslationDirection::kForOrchAgent,
                              port_translation_map, ir_table_entry));
  return ir_table_entry;
}

sonic::AppDbUpdates PiTableEntryUpdatesToIr(
    const p4::v1::WriteRequest& request, const pdpi::IrP4Info& p4_info,
    const boost::bimap<std::string, std::string>& port_translation_map,
    pdpi::IrWriteResponse* response, P4RuntimeTweaks* tweak) {
  sonic::AppDbUpdates ir_updates;
  for (const auto& update : request.updates()) {
    // An RPC response should be created for every updater.
    auto entry_status = response->add_statuses();
    ++ir_updates.total_rpc_updates;

    // If we cannot translate it then we should just report an error (i.e. do
    // not try to handle it in lower layers).
    auto ir_table_entry = DoPiTableEntryToIr(update.entity().table_entry(),
                                             p4_info, port_translation_map);
    *entry_status = GetIrUpdateStatus(ir_table_entry.status());
    if (!ir_table_entry.ok()) {
      LOG(WARNING) << "Could not translate PI to IR: "
                   << update.entity().table_entry().DebugString();
      continue;
    }

    int rpc_index = response->statuses_size() - 1;
    ir_updates.entries.push_back(
        sonic::AppDbEntry{.rpc_index = rpc_index,
                          .entry = tweak->ForOrchAgent(*ir_table_entry),
                          .update_type = update.type()});
  }
  return ir_updates;
}

}  // namespace

P4RuntimeImpl::P4RuntimeImpl(
    std::unique_ptr<swss::DBConnectorInterface> app_db_client,
    std::unique_ptr<swss::DBConnectorInterface> state_db_client,
    std::unique_ptr<swss::ProducerStateTableInterface> app_db_table_p4rt,
    std::unique_ptr<swss::ConsumerNotifierInterface> app_db_notifier_p4rt,
    std::unique_ptr<swss::ProducerStateTableInterface> app_db_table_vrf,
    std::unique_ptr<swss::ConsumerNotifierInterface> app_db_notifier_vrf,
    std::unique_ptr<sonic::PacketIoInterface> packetio_impl, bool use_genetlink)
    : app_db_client_(std::move(app_db_client)),
      state_db_client_(std::move(state_db_client)),
      app_db_table_p4rt_(std::move(app_db_table_p4rt)),
      app_db_notifier_p4rt_(std::move(app_db_notifier_p4rt)),
      app_db_table_vrf_(std::move(app_db_table_vrf)),
      app_db_notifier_vrf_(std::move(app_db_notifier_vrf)),
      packetio_impl_(std::move(packetio_impl)) {
  controller_manager_ = absl::make_unique<SdnControllerManager>();

  // Spawn the receiver thread to receive In packets.
  auto status_or = StartReceive(use_genetlink);
  if (status_or.ok()) {
    receive_thread_ = std::move(*status_or);
  } else {
    // TODO: Move to critical state.
    LOG(FATAL) << "Failed to spawn Receive thread, error: "
               << status_or.status();
  }
}

absl::Status SendPacketOut(
    const pdpi::IrP4Info& p4_info,
    const boost::bimap<std::string, std::string>& port_translation_map,
    sonic::PacketIoInterface* const packetio_impl,
    const p4::v1::PacketOut& packet) {
  // Convert to IR to check validity of PacketOut message (e.g. duplicate or
  // missing metadata fields).
  ASSIGN_OR_RETURN(auto ir, pdpi::PiPacketOutToIr(p4_info, packet));

  std::string egress_port_id;
  int submit_to_ingress = 0;
  // Parse the packet metadata to get the value of different attributes,
  for (const auto& meta : packet.metadata()) {
    switch (meta.metadata_id()) {
      case PACKET_OUT_EGRESS_PORT_ID: {
        egress_port_id = meta.value();
        break;
      }
      case PACKET_OUT_SUBMIT_TO_INGRESS_ID: {
        ASSIGN_OR_RETURN(submit_to_ingress,
                         pdpi::ArbitraryByteStringToUint(
                             meta.value(), SUBMIT_TO_INGRESS_BITWIDTH),
                         _.LogError() << "Unable to get inject_ingress "
                                         "from the packet metadata");
        break;
      }
      case PACKET_OUT_UNUSED_PAD_ID: {
        // Nothing to do.
        break;
      }
      default:
        return gutil::InvalidArgumentErrorBuilder()
               << "Unexpected Packet Out metadata id " << meta.metadata_id();
    }
  }

  std::string sonic_port_name;
  if (submit_to_ingress == 1) {
    // Use submit_to_ingress attribute value netdev port.
    sonic_port_name = std::string(sonic::kSubmitToIngress);
  } else {
    // Use egress_port_id attribute value.
    ASSIGN_OR_RETURN(sonic_port_name,
                     TranslatePort(PortTranslationDirection::kForOrchAgent,
                                   port_translation_map, egress_port_id));
  }

  // Send packet out via the socket.
  RETURN_IF_ERROR(
      packetio_impl->SendPacketOut(sonic_port_name, packet.payload()));

  return absl::OkStatus();
}

// Adds the given metadata to the PacketIn.
absl::StatusOr<p4::v1::PacketIn> CreatePacketInMessage(
    const std::string& source_port_id, const std::string& target_port_id) {
  p4::v1::PacketIn packet;
  p4::v1::PacketMetadata* metadata = packet.add_metadata();
  // Add Ingress port id.
  metadata->set_metadata_id(PACKET_IN_INGRESS_PORT_ID);
  metadata->set_value(source_port_id);

  // Add target egress port id.
  metadata = packet.add_metadata();
  metadata->set_metadata_id(PACKET_IN_TARGET_EGRESS_PORT_ID);
  metadata->set_value(target_port_id);

  return packet;
}

grpc::Status P4RuntimeImpl::Write(grpc::ServerContext* context,
                                  const p4::v1::WriteRequest* request,
                                  p4::v1::WriteResponse* response) {
#ifdef __EXCEPTIONS
  try {
#endif
    absl::MutexLock l(&server_state_lock_);
    // Only accept a write request if it is from the master controller.
    if (!controller_manager_->IsMasterElectionId(
            ToNativeUint128(request->election_id()))) {
      return grpc::Status(grpc::StatusCode::PERMISSION_DENIED,
                          "Only master controller can issue write requests.");
    }

    // We can only program the flow if the forwarding pipeline has been set.
    if (!ir_p4info_.has_value()) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Switch has not configured the forwarding pipeline.");
    }

    pdpi::IrWriteRpcStatus rpc_status;
    pdpi::IrWriteResponse* rpc_response = rpc_status.mutable_rpc_response();
    sonic::AppDbUpdates app_db_updates = PiTableEntryUpdatesToIr(
        *request, *ir_p4info_, port_translation_map_, rpc_response, &tweak_);

    // Any AppDb update failures should be appended to the `rpc_response`. If
    // UpdateAppDb fails we should go critical.
    auto app_db_write_status = sonic::UpdateAppDb(
        app_db_updates, *ir_p4info_, *app_db_table_p4rt_,
        *app_db_notifier_p4rt_, *app_db_client_, *state_db_client_,
        *app_db_table_vrf_, *app_db_notifier_vrf_, &vrf_id_reference_count_,
        rpc_response);
    if (!app_db_write_status.ok()) {
      // TODO: go into critical state.
      auto write_error =
          absl::StrCat("Encountered internal error during Write: ",
                       app_db_write_status.ToString());
      LOG(ERROR) << write_error;
      return grpc::Status(grpc::StatusCode::INTERNAL, write_error);
    }

    auto grpc_status = pdpi::IrWriteRpcStatusToGrpcStatus(rpc_status);
    if (!grpc_status.ok()) {
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          grpc_status.status().ToString());
    }
    return *grpc_status;
#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    LOG(FATAL) << "Exception caught in " << __func__ << ", error:" << e.what();
  } catch (...) {
    LOG(FATAL) << "Unknown exception caught in " << __func__;
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

    auto response_status =
        DoRead(*request, ir_p4info_.value(), port_translation_map_,
               *app_db_client_, &tweak_);
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
    LOG(FATAL) << "Exception caught in " << __func__ << ", error:" << e.what();
  } catch (...) {
    LOG(FATAL) << "Unknown exception caught in " << __func__;
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
    auto controller =
        absl::make_unique<SdnControllerConnection>(context, stream);
    p4::v1::StreamMessageRequest request;
    while (stream->Read(&request)) {
      switch (request.update_case()) {
        case p4::v1::StreamMessageRequest::kArbitration: {
          absl::MutexLock l(&server_state_lock_);
          LOG(INFO) << "[arbitration]: StreamChannel received master "
                       "arbitration update: "
                    << request.DebugString() << std::endl;
          auto status = controller_manager_->HandleArbitrationUpdate(
              request.arbitration(), controller.get());
          if (!status.ok()) {
            LOG(ERROR) << "Failed arbitration update: "
                       << status.error_message();
            controller_manager_->HandleControllerDisconnect(controller.get());
            return status;
          }
          break;
        }
        case p4::v1::StreamMessageRequest::kPacket: {
          absl::MutexLock l(&server_state_lock_);
          // Returns with an error if the write request was not received from a
          // master controller
          auto is_master = controller_manager_->IsMasterElectionId(
              controller->GetElectionId().value_or(0));
          if (!is_master) {
            controller->SendStreamMessageResponse(GenerateErrorResponse(
                gutil::PermissionDeniedErrorBuilder().LogError()
                    << "Cannot process request. Only the master controller can "
                       "send PacketOuts.",
                request.packet()));
          } else {
            if (!ir_p4info_.has_value()) {
              controller->SendStreamMessageResponse(GenerateErrorResponse(
                  gutil::FailedPreconditionErrorBuilder().LogError()
                  << "Cannot send packet out. Switch has no "
                     "ForwardingPipelineConfig."));
            } else {
              auto status =
                  SendPacketOut(ir_p4info_.value(), port_translation_map_,
                                packetio_impl_.get(), request.packet());
              if (!status.ok()) {
                // Get master streamchannel and write into the stream.
                controller_manager_->SendStreamMessageToMaster(
                    GenerateErrorResponse(
                        gutil::StatusBuilder(status).LogError()
                            << "Failed to send packet out.",
                        request.packet()));
              }
            }
          }
          break;
        }
        case p4::v1::StreamMessageRequest::kDigestAck:
        case p4::v1::StreamMessageRequest::kOther:
        default:
          controller->SendStreamMessageResponse(
              GenerateErrorResponse(gutil::UnimplementedErrorBuilder()
                                    << "Stream update type is not supported."));
          LOG(ERROR) << "Received unhandled stream channel message: "
                     << request.DebugString();
      }
    }
    {
      absl::MutexLock l(&server_state_lock_);
      controller_manager_->HandleControllerDisconnect(controller.get());
    }
    return grpc::Status::OK;
#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    LOG(FATAL) << "Exception caught in " << __func__ << ", error:" << e.what();
  } catch (...) {
    LOG(FATAL) << "Unknown exception caught in " << __func__;
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
        << ToNativeUint128(request->election_id());
    if (!controller_manager_->IsMasterElectionId(
            ToNativeUint128(request->election_id()))) {
      return AbslStatusToGrpcStatus(
          gutil::PermissionDeniedErrorBuilder().LogError()
          << "SetForwardingPipelineConfig is only available to the master "
          << "controller.");
    }

    if (request->action() !=
        p4::v1::SetForwardingPipelineConfigRequest::VERIFY_AND_COMMIT) {
      return AbslStatusToGrpcStatus(
          gutil::UnimplementedErrorBuilder().LogError()
          << "Only Action::VERIFY_AND_COMMIT is supported for "
          << "SetForwardingPipelineConfig.");
    }

    {
      absl::Status validate_result = ValidateP4Info(request->config().p4info());
      if (!validate_result.ok()) {
        // TODO (b/181241450): Re-enable verification checks before SB400 DVT
        // end.
        LOG(WARNING) << "P4Info is not valid. Details: " << validate_result;
        /*
        return gutil::AbslStatusToGrpcStatus(
            gutil::StatusBuilder(validate_result.code()).LogError()
            << "P4Info is not valid. Details: " << validate_result.message());
        */
      }
    }

    auto ir_p4info_result = pdpi::CreateIrP4Info(request->config().p4info());
    if (!ir_p4info_result.ok())
      return gutil::AbslStatusToGrpcStatus(ir_p4info_result.status());
    pdpi::IrP4Info new_ir_p4info = std::move(ir_p4info_result.value());
    tweak_.ForOrchAgent(new_ir_p4info);

    if (!ir_p4info_.has_value()) {
      // Apply a config if we don't currently have one.
      absl::Status config_result = ApplyForwardingPipelineConfig(new_ir_p4info);
      if (!config_result.ok()) {
        LOG(ERROR) << "Failed to apply ForwardingPipelineConfig: "
                   << config_result;
        return gutil::AbslStatusToGrpcStatus(config_result);
      }
      ir_p4info_ = std::move(new_ir_p4info);
    } else {
      // Fail if the new forwarding pipeline is different from the current one.
      std::string diff_report;
      if (!IrP4InfoEquals(ir_p4info_.value(), new_ir_p4info, &diff_report)) {
        return gutil::AbslStatusToGrpcStatus(
            gutil::UnimplementedErrorBuilder().LogError()
            << "Modifying a configured forwarding pipeline is not currently "
               "supported. Please reboot the device. Configuration "
               "differences:\n"
            << diff_report);
      }
    }
    forwarding_pipeline_config_ = request->config();
    LOG(INFO) << "SetForwardingPipelineConfig completed successfully.";
    // Collect any port ID to port name translations;
    auto port_map_result = sonic::GetPortIdTranslationMap(*app_db_client_);
    if (!port_map_result.ok()) {
      return gutil::AbslStatusToGrpcStatus(port_map_result.status());
    }
    port_translation_map_ = *port_map_result;
    LOG(INFO) << "Collected port ID to port name mappings.";

#ifdef __EXCEPTIONS
  } catch (const std::exception& e) {
    LOG(FATAL) << "Exception caught in " << __func__ << ", error:" << e.what();
  } catch (...) {
    LOG(FATAL) << "Unknown exception caught in " << __func__;
  }
#endif

  return grpc::Status(grpc::StatusCode::OK, "");
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
    LOG(FATAL) << "Exception caught in " << __func__ << ", error:" << e.what();
  } catch (...) {
    LOG(FATAL) << "Unknown exception caught in " << __func__;
  }
#endif
}

absl::Status P4RuntimeImpl::ApplyForwardingPipelineConfig(
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
      // Prefix P4RT: to the key and form the keys vector.
      std::vector<std::string> keys;
      keys.push_back(
          absl::StrCat(app_db_table_p4rt_->get_table_name(), ":", acl_key));
      pdpi::IrWriteResponse ir_write_response;
      RETURN_IF_ERROR(sonic::GetAndProcessResponseNotification(
          keys, 1, *app_db_notifier_p4rt_, *app_db_client_, *state_db_client_,
          ir_write_response));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::thread> P4RuntimeImpl::StartReceive(bool use_genetlink) {
  // Define the lambda function for the callback to be executed for every
  // receive packet.
  auto SendPacketInToController =
      [this](const std::string& source_port_name,
             const std::string& target_port_name,
             const std::string& payload) -> absl::Status {
    // Convert Sonic port name to controller port number.
    ASSIGN_OR_RETURN(std::string source_port_id,
                     TranslatePort(PortTranslationDirection::kForController,
                                   port_translation_map_, source_port_name),
                     _.SetCode(absl::StatusCode::kInternal).LogError()
                         << "Failed to parse source port");

    // TODO: Until string port names are supported, re-assign empty
    // target egress port names to match the ingress port.
    std::string target_port_id = source_port_id;
    if (!target_port_name.empty()) {
      ASSIGN_OR_RETURN(target_port_id,
                       TranslatePort(PortTranslationDirection::kForController,
                                     port_translation_map_, target_port_name),
                       _.SetCode(absl::StatusCode::kInternal).LogError()
                           << "Failed to parse target port");
    }

    // Form the PacketIn metadata fields before writing into the
    // stream.
    ASSIGN_OR_RETURN(auto packet_in,
                     CreatePacketInMessage(source_port_id, target_port_id));
    p4::v1::StreamMessageResponse response;
    *response.mutable_packet() = packet_in;
    *response.mutable_packet()->mutable_payload() = payload;
    absl::MutexLock l(&server_state_lock_);
    // Get master streamchannel and write into the stream.
    controller_manager_->SendStreamMessageToMaster(response);
    return absl::OkStatus();
  };

  absl::MutexLock l(&server_state_lock_);
  // Now that all packet io ports have been discovered, start the receive thread
  // that will wait for in packets.
  return packetio_impl_->StartReceive(SendPacketInToController, use_genetlink);
}

}  // namespace p4rt_app

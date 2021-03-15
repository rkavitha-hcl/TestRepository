#include "sai_p4/instantiations/google/sai_p4info.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/middleblock_p4info_embed.h"
#include "sai_p4/instantiations/google/switch_role.h"
#include "sai_p4/instantiations/google/wbb_p4info_embed.h"

namespace sai {

using ::google::protobuf::TextFormat;
using ::gutil::FileToc;
using p4::config::v1::P4Info;
using pdpi::IrP4Info;

namespace {

P4Info* FileTocToP4Info(const FileToc* const toc) {
  std::string data(toc[0].data, toc[0].size);
  P4Info* info = new P4Info();
  CHECK(  // Crash ok: TAP rules out failures.
      TextFormat::ParseFromString(data, info))
      << "unable to read embedded p4info text file";
  return info;
}

IrP4Info* CreateIrP4Info(const P4Info& info) {
  absl::StatusOr<IrP4Info> ir_p4info = pdpi::CreateIrP4Info(info);
  CHECK(ir_p4info.status().ok())  // Crash ok: TAP rules out failures.
      << ir_p4info.status();
  return new IrP4Info(std::move(ir_p4info).value());
}

}  // namespace

// Adapted from go/totw/128.
const P4Info& GetP4Info(SwitchRole role) {
  // Safe static object initialization following go/totw/110.
  static const absl::flat_hash_map<SwitchRole, P4Info*>* role_to_info = [] {
    return new absl::flat_hash_map<SwitchRole, P4Info*>(
        {{SwitchRole::kMiddleblock,
          FileTocToP4Info(middleblock_p4info_embed_create())},
         {SwitchRole::kWbb, FileTocToP4Info(wbb_p4info_embed_create())}});
  }();
  static const P4Info* empty_info = [] { return new P4Info(); }();
  if (role_to_info->contains(role)) {
    return *role_to_info->at(role);
  }
  LOG(DFATAL) << "Obtaining P4Info for invalid role: "
              << static_cast<int>(role);
  return *empty_info;
}

const IrP4Info& GetIrP4Info(SwitchRole role) {
  // Safe static object initialization following go/totw/110.
  static const absl::flat_hash_map<SwitchRole, IrP4Info*>* role_to_info = [] {
    auto result = new absl::flat_hash_map<SwitchRole, IrP4Info*>();
    for (SwitchRole role : AllSwitchRoles()) {
      result->insert({role, CreateIrP4Info(GetP4Info(role))});
    }
    return result;
  }();
  static const IrP4Info* empty_info = [] { return new IrP4Info(); }();
  if (role_to_info->contains(role)) {
    return *role_to_info->at(role);
  }
  LOG(DFATAL) << "Obtaining IrP4Info for invalid role: "
              << static_cast<int>(role);
  return *empty_info;
}

}  // namespace sai

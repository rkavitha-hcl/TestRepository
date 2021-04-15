#include "sai_p4/instantiations/google/sai_p4info.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/middleblock_p4info_embed.h"
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
const P4Info& GetP4Info(Instantiation instantiation) {
  // Safe static object initialization following go/totw/110.
  static const absl::flat_hash_map<Instantiation,
                                   P4Info*>* instantiation_to_info = [] {
    return new absl::flat_hash_map<Instantiation, P4Info*>(
        {{Instantiation::kMiddleblock,
          FileTocToP4Info(middleblock_p4info_embed_create())},
         {Instantiation::kWbb, FileTocToP4Info(wbb_p4info_embed_create())}});
  }();
  static const P4Info* empty_info = [] { return new P4Info(); }();
  if (instantiation_to_info->contains(instantiation)) {
    return *instantiation_to_info->at(instantiation);
  }
  LOG(DFATAL) << "Obtaining P4Info for invalid instantiation: "
              << static_cast<int>(instantiation);
  return *empty_info;
}

const IrP4Info& GetIrP4Info(Instantiation instantiation) {
  // Safe static object initialization following go/totw/110.
  static const absl::flat_hash_map<Instantiation,
                                   IrP4Info*>* instantiation_to_info = [] {
    auto result = new absl::flat_hash_map<Instantiation, IrP4Info*>();
    for (Instantiation instantiation : AllInstantiations()) {
      result->insert({instantiation, CreateIrP4Info(GetP4Info(instantiation))});
    }
    return result;
  }();
  static const IrP4Info* empty_info = [] { return new IrP4Info(); }();
  if (instantiation_to_info->contains(instantiation)) {
    return *instantiation_to_info->at(instantiation);
  }
  LOG(DFATAL) << "Obtaining IrP4Info for invalid instantiation: "
              << static_cast<int>(instantiation);
  return *empty_info;
}

}  // namespace sai

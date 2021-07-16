#include "sai_p4/instantiations/google/sai_p4info.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/p4info_union_lib.h"
#include "sai_p4/instantiations/google/fabric_border_router_p4info_embed.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/middleblock_p4info_embed.h"
#include "sai_p4/instantiations/google/unioned_p4info_embed.h"
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

const P4Info& GetP4Info(Instantiation instantiation) {
  static const P4Info* const kFabricBorderRouterP4Info =
      FileTocToP4Info(fabric_border_router_p4info_embed_create());
  static const P4Info* const kMiddleblockP4Info =
      FileTocToP4Info(middleblock_p4info_embed_create());
  static const P4Info* const kWbbP4Info =
      FileTocToP4Info(wbb_p4info_embed_create());

  switch (instantiation) {
    case Instantiation::kFabricBorderRouter:
      return *kFabricBorderRouterP4Info;
    case Instantiation::kMiddleblock:
      return *kMiddleblockP4Info;
    case Instantiation::kWbb:
      return *kWbbP4Info;
  }
  LOG(DFATAL) << "Obtaining P4Info for invalid instantiation: "
              << static_cast<int>(instantiation);

  static const P4Info* const kEmptyP4Info = new P4Info();
  return *kEmptyP4Info;
}

const IrP4Info& GetIrP4Info(Instantiation instantiation) {
  static const IrP4Info* const kFabricBorderRouterIrP4Info =
      CreateIrP4Info(GetP4Info(Instantiation::kFabricBorderRouter));
  static const IrP4Info* const kMiddleblockIrP4Info =
      CreateIrP4Info(GetP4Info(Instantiation::kMiddleblock));
  static const IrP4Info* const kWbbIrP4Info =
      CreateIrP4Info(GetP4Info(Instantiation::kWbb));

  switch (instantiation) {
    case Instantiation::kFabricBorderRouter:
      return *kFabricBorderRouterIrP4Info;
    case Instantiation::kMiddleblock:
      return *kMiddleblockIrP4Info;
    case Instantiation::kWbb:
      return *kWbbIrP4Info;
  }
  LOG(DFATAL) << "Obtaining P4Info for invalid instantiation: "
              << static_cast<int>(instantiation);
  static const IrP4Info* const kEmptyIrP4Info = new IrP4Info();
  return *kEmptyIrP4Info;
}

const p4::config::v1::P4Info& GetUnionedP4Info() {
  static const P4Info* const unioned_p4info =
      FileTocToP4Info(unioned_p4info_embed_create());
  return *unioned_p4info;
}

}  // namespace sai

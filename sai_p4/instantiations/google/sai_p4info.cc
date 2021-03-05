#include "sai_p4/instantiations/google/sai_p4info.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/sai_p4info_embed.h"

namespace sai {

using ::google::protobuf::TextFormat;
using ::gutil::FileToc;
using p4::config::v1::P4Info;
using pdpi::IrP4Info;

// Adapted from go/totw/128.
const P4Info& GetP4Info(SwitchRole role) {
  // Safe static object initialization following go/totw/110.
  static const P4Info* info = [] {
    const FileToc* const toc = sai_p4info_embed_create();
    std::string data(toc[0].data, toc[0].size);
    P4Info* info = new P4Info();
    CHECK(  // Crash ok: TAP rules out failures.
        TextFormat::ParseFromString(data, info))
        << "unable to read embedded p4info text file";
    return info;
  }();
  return *info;
}

const IrP4Info& GetIrP4Info(SwitchRole role) {
  // Safe static object initialization following go/totw/110.
  static const IrP4Info* ir_info = [role] {
    const P4Info& info = GetP4Info(role);
    if (info.ByteSize() == 0) return new IrP4Info();
    absl::StatusOr<IrP4Info> ir_p4info = pdpi::CreateIrP4Info(info);
    CHECK(ir_p4info.status().ok())  // Crash ok: TAP rules out failures.
        << ir_p4info.status();
    return new IrP4Info(std::move(ir_p4info).value());
  }();
  return *ir_info;
}

}  // namespace sai

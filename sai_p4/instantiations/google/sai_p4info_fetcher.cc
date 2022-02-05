#include "sai_p4/instantiations/google/sai_p4info_fetcher.h"

#include <optional>

#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/fabric_border_router_p4info_embed.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/middleblock_with_s2_ecmp_profile_p4info_embed.h"
#include "sai_p4/instantiations/google/middleblock_with_s3_ecmp_profile_p4info_embed.h"
#include "sai_p4/instantiations/google/unioned_p4info_embed.h"
#include "sai_p4/instantiations/google/wbb_p4info_embed.h"
#include "sai_p4/tools/p4info_tools.h"

namespace sai {
namespace {
using ::google::protobuf::TextFormat;
using ::gutil::FileToc;
using ::p4::config::v1::P4Info;

P4Info FileTocToP4Info(const FileToc* const toc) {
  std::string data(toc[0].data, toc[0].size);
  P4Info info;
  CHECK(  // Crash ok: TAP rules out failures.
      TextFormat::ParseFromString(data, &info))
      << "unable to read embedded p4info text file";
  return info;
}

// Returns the middleblock P4Info at the provided stage. If the stage is not
// defined, returns the stage 2 P4Info by default.
P4Info MiddleblockP4Info(std::optional<ClosStage> stage) {
  if (stage.has_value()) {
    switch (*stage) {
      case ClosStage::kStage2:
        return FileTocToP4Info(
            middleblock_with_s2_ecmp_profile_p4info_embed_create());
      case ClosStage::kStage3:
        return FileTocToP4Info(
            middleblock_with_s3_ecmp_profile_p4info_embed_create());
    }
  }
  return FileTocToP4Info(
      middleblock_with_s2_ecmp_profile_p4info_embed_create());
}

}  // namespace

P4Info FetchP4Info(Instantiation instantiation,
                   std::optional<ClosStage> stage) {
  P4Info p4info;
  switch (instantiation) {
    case Instantiation::kMiddleblock:
      p4info = MiddleblockP4Info(stage);
      break;
    case Instantiation::kFabricBorderRouter:
      p4info = FileTocToP4Info(fabric_border_router_p4info_embed_create());
      break;
    case Instantiation::kWbb:
      p4info = FileTocToP4Info(wbb_p4info_embed_create());
      break;
  }
  return p4info;
}

P4Info FetchUnionedP4Info() {
  return FileTocToP4Info(unioned_p4info_embed_create());
}

bool DiffersByClosStage(Instantiation instantiation) {
  switch (instantiation) {
    case Instantiation::kMiddleblock:
    case Instantiation::kFabricBorderRouter:
      return true;
    case Instantiation::kWbb:
      return false;
  }
  LOG(DFATAL) << absl::StrCat("invalid instantiation '$0'",
                              static_cast<int>(instantiation));
  return false;
}

absl::Status AssertInstantiationAndClosStageAreCompatible(
    Instantiation instantiation, std::optional<ClosStage> stage) {
  // If an instantiation admits different CLOS stages, then a CLOS stage must be
  // given.
  if (sai::DiffersByClosStage(instantiation) && !stage.has_value()) {
    return absl::InvalidArgumentError(
        absl::Substitute("Instantiation '$0' exists for different CLOS stages, "
                         "but no CLOS stage was given.",
                         sai::InstantiationToString(instantiation)));
  }
  // Otherwise, a CLOS stage may not be given.
  if (!sai::DiffersByClosStage(instantiation) && stage.has_value()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Instantiation '$0' does not exist for different CLOS stages, "
        "but CLOS stage $1 was given.",
        sai::InstantiationToString(instantiation),
        sai::ClosStageToString(*stage)));
  }
  return absl::OkStatus();
}

}  // namespace sai

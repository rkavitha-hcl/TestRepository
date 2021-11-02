#include "sai_p4/instantiations/google/sai_nonstandard_platforms.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "sai_p4/instantiations/google/sai_nonstandard_platforms_embed.h"

namespace sai {

namespace {

using ::p4::config::v1::P4Info;

// Return the base (no suffix) name of the instantiation p4info file.
std::string InstantiationName(Instantiation instantiation) {
  // Default to the s2_ecmp_profile for middleblock.
  if (instantiation == Instantiation::kMiddleblock) {
    return "middleblock_with_s2_ecmp_profile";
  }
  return InstantiationToString(instantiation);
}

// Return the name of the P4Config JSON file.
std::string P4ConfigName(Instantiation instantiation,
                         NonstandardPlatform platform) {
  return absl::StrFormat("sai_%s_%s.config.json",
                         InstantiationName(instantiation),
                         PlatformName(platform));
}

// Return the name of the P4Info protobuf file.
std::string P4InfoName(Instantiation instantiation,
                       NonstandardPlatform platform) {
  return absl::StrFormat("sai_%s_%s.p4info.pb.txt",
                         InstantiationName(instantiation),
                         PlatformName(platform));
}

}  // namespace

std::string PlatformName(NonstandardPlatform platform) {
  switch (platform) {
    case NonstandardPlatform::kBmv2:
      return "bmv2";
    case NonstandardPlatform::kP4Symbolic:
      return "p4_symbolic";
  }
  LOG(DFATAL) << "invalid NonstandardPlattform: " << static_cast<int>(platform);
  return "";
}

std::string GetNonstandardP4Config(Instantiation instantiation,
                                   NonstandardPlatform platform) {
  const gutil::FileToc* toc = sai_nonstandard_platforms_embed_create();
  std::string key = P4ConfigName(instantiation, platform);
  for (int i = 0; i < sai_nonstandard_platforms_embed_size(); ++i) {
    if (toc[i].name == key) {
      // We use round robin hashing for nonstandard platforms, which makes it
      // easy to predict all possible output through repeated simulation.
      return absl::StrReplaceAll(toc[i].data, {{R"("algo" : "identity")",
                                                R"("algo" : "round_robin")"}});
    }
  }
  LOG(DFATAL) << "couldn't find P4 config for instantiation '"
              << InstantiationToString(instantiation) << "' and platform '"
              << PlatformName(platform) << "': key '" << key << "'"
              << " not found in table of contents";
  return "";
}

P4Info GetNonstandardP4Info(Instantiation instantiation,
                            NonstandardPlatform platform) {
  P4Info p4info;
  const gutil::FileToc* toc = sai_nonstandard_platforms_embed_create();
  std::string key = P4InfoName(instantiation, platform);
  for (int i = 0; i < sai_nonstandard_platforms_embed_size(); ++i) {
    if (toc[i].name == key) {
      CHECK(  // Crash ok: TAP rules out failures.
          google::protobuf::TextFormat::ParseFromString(toc[i].data, &p4info))
          << "unable to parse embedded P4 info file '" << key
          << "': " << toc[i].data;
      return p4info;
    }
  }
  LOG(DFATAL) << "couldn't find P4 info for instantiation '"
              << InstantiationToString(instantiation) << "' and platform '"
              << PlatformName(platform) << "': key '" << key << "'"
              << " not found in table of contents";
  return p4info;
}

}  // namespace sai

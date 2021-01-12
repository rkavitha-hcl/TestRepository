#include <memory>

#include "google_sai_p4/sai_p4info.h"
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "p4_fuzzer/annotation_util.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer.pb.h"
#include "p4_fuzzer/mutation.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/pd.h"

namespace p4_fuzzer {
namespace {

TEST(FuzzerShowcaseTest, EntryGeneration) {
  absl::BitGen gen;

  pdpi::IrP4Info ir_p4_info = sai::GetIrP4Info();

  std::vector<AnnotatedTableEntry> valid_entries =
      ValidForwardingEntries(&gen, ir_p4_info, 1000);

  SwitchState state(ir_p4_info);

  for (int i = 0; i < 1000; i++) {
    AnnotatedWriteRequest request = FuzzWriteRequest(&gen, ir_p4_info, state);
  }
}

}  // namespace
}  // namespace p4_fuzzer

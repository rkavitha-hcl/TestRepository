#ifndef GOOGLE_P4_FUZZER_FUZZER_TESTs_H_
#define GOOGLE_P4_FUZZER_FUZZER_TESTs_H_

#include "thinkit/mirror_testbed.h"

namespace p4_fuzzer {

void FuzzP4rtWriteAndCheckNoInternalErrors(thinkit::MirrorTestbed& testbed,
                                           bool mask_known_failures);

}  // namespace p4_fuzzer

#endif  // GOOGLE_P4_FUZZER_FUZZER_TESTs_H_

#include "p4_fuzzer/switch_state.h"

#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/pd.h"
#include "p4_pdpi/testing/main_p4_pd.pb.h"
#include "p4_pdpi/testing/test_p4info.h"

namespace p4_fuzzer {
namespace {

using ::p4::config::v1::P4Info;
using ::p4::config::v1::Preamble;
using ::p4::config::v1::Table;
using ::p4::v1::TableEntry;
using ::p4::v1::Update;
using ::pdpi::CreateIrP4Info;
using ::pdpi::IrP4Info;

TEST(SwitchStateTest, TableEmptyTrivial) {
  IrP4Info info;
  SwitchState state(info);

  EXPECT_TRUE(state.AllTablesEmpty());
}

TEST(SwitchStateTest, TableEmptyFromP4Info) {
  P4Info info;
  Table* ptable = info.add_tables();
  ptable->mutable_preamble()->set_id(42);

  IrP4Info ir_info = CreateIrP4Info(info).value();

  SwitchState state(ir_info);
  EXPECT_TRUE(state.AllTablesEmpty());
}

TEST(SwitchStateTest, RuleInsert) {
  P4Info info;
  Table* ptable = info.add_tables();
  Preamble* preamble = ptable->mutable_preamble();
  preamble->set_id(42);
  preamble->set_alias("Spam");

  ptable = info.add_tables();
  preamble = ptable->mutable_preamble();
  preamble->set_id(43);
  preamble->set_alias("Eggs");

  IrP4Info ir_info = CreateIrP4Info(info).value();

  SwitchState state(ir_info);

  Update update;
  update.set_type(Update::INSERT);

  TableEntry* entry = update.mutable_entity()->mutable_table_entry();
  entry->set_table_id(42);

  ASSERT_OK(state.ApplyUpdate(update));

  EXPECT_FALSE(state.AllTablesEmpty());
  EXPECT_FALSE(state.IsTableEmpty(42));
  EXPECT_TRUE(state.IsTableEmpty(43));

  EXPECT_EQ(state.GetNumTableEntries(42), 1);
  EXPECT_EQ(state.GetNumTableEntries(43), 0);

  EXPECT_EQ(state.GetTableEntries(42).size(), 1);
  EXPECT_EQ(state.GetTableEntries(43).size(), 0);
}

TEST(SwitchStateTest, RuleDelete) {
  P4Info info;
  Table* ptable = info.add_tables();
  Preamble* preamble = ptable->mutable_preamble();
  preamble->set_id(42);
  preamble->set_alias("Spam");

  ptable = info.add_tables();
  preamble = ptable->mutable_preamble();
  preamble->set_id(43);
  preamble->set_alias("Eggs");

  IrP4Info ir_info = CreateIrP4Info(info).value();

  SwitchState state(ir_info);

  Update update;
  update.set_type(Update::INSERT);

  TableEntry* entry = update.mutable_entity()->mutable_table_entry();
  entry->set_table_id(42);

  ASSERT_OK(state.ApplyUpdate(update));

  update.set_type(Update::DELETE);
  ASSERT_OK(state.ApplyUpdate(update));

  EXPECT_TRUE(state.AllTablesEmpty());

  EXPECT_EQ(state.GetNumTableEntries(42), 0);
  EXPECT_EQ(state.GetTableEntries(42).size(), 0);
}

Update MakePiUpdate(const pdpi::IrP4Info& info, Update::Type type,
                    const pdpi::TableEntry& entry) {
  pdpi::Update pd;
  pd.set_type(type);
  *pd.mutable_table_entry() = entry;
  auto pi = pdpi::PdUpdateToPi(info, pd);
  CHECK_OK(pi.status());  // Crash ok
  return *pi;
}

TEST(SwitchStateTest, GetIdsForMatchField) {
  IrP4Info info = pdpi::GetTestIrP4Info();
  SwitchState state(info);
  pdpi::IrMatchFieldReference field;
  field.set_table("referred_table");
  field.set_match_field("id");
  ASSERT_THAT(state.GetIdsForMatchField(field), testing::IsEmpty());

  pdpi::TableEntry entry1 = gutil::ParseProtoOrDie<pdpi::TableEntry>(
      R"PB(
        referred_table_entry {
          match { id: "some-id" }
          action { do_thing_4 {} }
        }
      )PB");
  ASSERT_OK(state.ApplyUpdate(MakePiUpdate(info, Update::INSERT, entry1)));
  ASSERT_THAT(state.GetIdsForMatchField(field),
              testing::UnorderedElementsAre("some-id"));

  pdpi::TableEntry entry2 = gutil::ParseProtoOrDie<pdpi::TableEntry>(
      R"PB(
        referred_table_entry {
          match { id: "other-id" }
          action { do_thing_4 {} }
        }
      )PB");
  ASSERT_OK(state.ApplyUpdate(MakePiUpdate(info, Update::INSERT, entry2)));
  ASSERT_THAT(state.GetIdsForMatchField(field),
              testing::UnorderedElementsAre("some-id", "other-id"));

  ASSERT_OK(state.ApplyUpdate(MakePiUpdate(info, Update::DELETE, entry1)));
  ASSERT_THAT(state.GetIdsForMatchField(field),
              testing::UnorderedElementsAre("other-id"));
}

}  // namespace
}  // namespace p4_fuzzer

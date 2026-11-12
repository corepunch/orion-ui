#include "test_framework.h"
#include <orion/ui.h>
#include <libxml/tree.h>
#include <stdint.h>

typedef struct { int id; int64_t amount; } int64_row_t;

static void test_int64_field(void) {
  TEST("int64 fields preserve exact limits through XML, parsing and record comparison");
  const db_field_meta_t fields[] = {
    { .name = "id", .type = DB_TYPE_INT, .offset = offsetof(int64_row_t, id) },
    { .name = "amount", .type = DB_TYPE_INT64, .offset = offsetof(int64_row_t, amount) }
  };
  int64_row_t source = { .id = 3, .amount = INT64_MAX }, restored = {0};
  xmlNodePtr node = xmlNewNode(NULL, BAD_CAST "row"); ASSERT_NOT_NULL(node);
  ASSERT_TRUE(db_save_field_to_xml(node, &source, &fields[1]));
  ASSERT_TRUE(db_load_field_from_xml(node, &restored, &fields[1]));
  ASSERT_EQUAL(restored.amount, INT64_MAX);
  source.amount = INT64_MIN;
  ASSERT_TRUE(db_save_field_to_xml(node, &source, &fields[1]));
  ASSERT_TRUE(db_load_field_from_xml(node, &restored, &fields[1]));
  ASSERT_EQUAL(restored.amount, INT64_MIN);
  ASSERT_FALSE(db_parse_field(&fields[1], &restored, "9223372036854775808"));
  ASSERT_EQUAL(restored.amount, INT64_MIN);
  ASSERT_FALSE(db_parse_field(&fields[1], &restored, "12abc"));
  ASSERT_EQUAL(restored.amount, INT64_MIN);
  restored.id = source.id;
  ASSERT_TRUE(db_record_equal(fields, 2, &source, &restored));
  restored.amount++;
  ASSERT_FALSE(db_record_equal(fields, 2, &source, &restored));
  xmlFreeNode(node);
  PASS();
}

static void test_localized_int64(void) {
  TEST("localized int64 parsing rejects malformed grouping without losing precision");
  int64_t value = 0; char text[64];
  ASSERT_TRUE(ui_set_language("de"));
  ASSERT_TRUE(ui_add_string_catalog_dir("share/locale"));
  ASSERT_TRUE(ui_parse_int64("9.007.199.254.740.993", &value));
  ASSERT_EQUAL(value, INT64_C(9007199254740993));
  ASSERT_STR_EQUAL(ui_format_int64(value, text, sizeof(text)), "9.007.199.254.740.993");
  ASSERT_FALSE(ui_parse_int64("1.5", &value));
  ASSERT_FALSE(ui_parse_int64("1.00", &value));
  ASSERT_TRUE(ui_set_language("en"));
  ASSERT_FALSE(ui_parse_int64("1,2", &value));
  ASSERT_TRUE(ui_parse_int64("9007199254740993", &value));
  ASSERT_EQUAL(value, INT64_C(9007199254740993));
  PASS();
}

int main(void) { TEST_START("64-bit fields"); test_int64_field(); test_localized_int64(); TEST_END(); }

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

#include <unity.h>

#include "heatboard/heat_list.h"
#include "test_suites.h"

using heatboard::Heat;
using heatboard::HeatListParseResult;
using heatboard::parseHeatListCsv;

namespace {

// Rows copied from the organiser's sheet: heats with a vacant last slot (5, 23), a vacant
// first slot (7), Japanese class names, and no newline after the last row.
constexpr std::string_view kRealExcerpt =
    "JDLid,name,class,heat\n"
    "J26785,Lee Subeen,Open,4\n"
    "J25730,辻 和彦,Open,4\n"
    "J26768,森下 琴康,Open,4\n"
    "J24708,金子 明日美,Open,4\n"
    "J25728,萩原 勝彦,Open,5\n"
    "J17031,黒川 新介,Open,5\n"
    "J24714,楠川 智久,Open,5\n"
    ",,Open,5\n"
    ",,Expert,7\n"
    "J24727,市川 一幸,Expert,7\n"
    "J25754,Song Jiwon,Expert,7\n"
    "J26772,Bae Woong Chan,Expert,7\n"
    "J21410,橋本 勇希,準々決勝 Pro,23\n"
    "J25737,平中 星凪,準々決勝 Pro,23\n"
    "J23605,犬飼 彩咲,準々決勝 Pro,23\n"
    ",,準々決勝 Pro,23\n"
    "J21410,橋本 勇希,決勝 Pro,38\n"
    "J20317,山本 悠貴,決勝 Pro,38\n"
    "J18129,高野 奏多,決勝 Pro,38\n"
    "J24707,犬飼 大葵,決勝 Pro,38";

void assertHeat(const Heat& heat, int number, const char* className, std::initializer_list<const char*> pilots) {
  TEST_ASSERT_EQUAL_INT(number, heat.number);
  TEST_ASSERT_EQUAL_STRING(className, heat.className.c_str());
  TEST_ASSERT_EQUAL_size_t(pilots.size(), heat.pilots.size());
  std::size_t slot = 0;
  for (const char* pilot : pilots) {
    TEST_ASSERT_EQUAL_STRING(pilot, heat.pilots[slot++].c_str());
  }
}

HeatListParseResult parseOk(std::string_view csv, std::size_t expectedHeats) {
  HeatListParseResult result = parseHeatListCsv(csv);
  TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
  TEST_ASSERT_EQUAL_STRING("", result.error.c_str());
  TEST_ASSERT_EQUAL_size_t(expectedHeats, result.heats.size());
  return result;
}

// `expectedInError` is the part of the message the firmware's log reader relies on
// (usually the "line N:" prefix).
void assertRejected(std::string_view csv, const char* expectedInError) {
  const HeatListParseResult result = parseHeatListCsv(csv);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_TRUE(result.heats.empty());
  TEST_ASSERT_FALSE(result.error.empty());
  TEST_ASSERT_TRUE_MESSAGE(result.error.find(expectedInError) != std::string::npos, result.error.c_str());
}

void test_csv_real_data_excerpt() {
  const HeatListParseResult result = parseOk(kRealExcerpt, 5);
  assertHeat(result.heats[0], 4, "Open", {"Lee Subeen", "辻 和彦", "森下 琴康", "金子 明日美"});
  assertHeat(result.heats[1], 5, "Open", {"萩原 勝彦", "黒川 新介", "楠川 智久", ""});
  assertHeat(result.heats[2], 7, "Expert", {"", "市川 一幸", "Song Jiwon", "Bae Woong Chan"});
  assertHeat(result.heats[3], 23, "準々決勝 Pro", {"橋本 勇希", "平中 星凪", "犬飼 彩咲", ""});
  assertHeat(result.heats[4], 38, "決勝 Pro", {"橋本 勇希", "山本 悠貴", "高野 奏多", "犬飼 大葵"});
}

void test_csv_crlf_line_endings() {
  const HeatListParseResult result = parseOk("JDLid,name,class,heat\r\nJ1,Alice,Open,1\r\n,,Open,1\r\nJ2,Bob,Pro,2\r\n", 2);
  assertHeat(result.heats[0], 1, "Open", {"Alice", ""});
  assertHeat(result.heats[1], 2, "Pro", {"Bob"});
}

void test_csv_utf8_bom() {
  const HeatListParseResult result = parseOk("\xEF\xBB\xBF" "JDLid,name,class,heat\nJ1,Alice,Open,1\n", 1);
  assertHeat(result.heats[0], 1, "Open", {"Alice"});
}

void test_csv_bom_without_header() {
  // The BOM must not end up glued to the first field or defeat header detection.
  const HeatListParseResult result = parseOk("\xEF\xBB\xBF" "J1,Alice,Open,1\n", 1);
  assertHeat(result.heats[0], 1, "Open", {"Alice"});
}

void test_csv_without_header() {
  const HeatListParseResult result = parseOk("J1,Alice,Open,1\nJ2,Bob,Open,1\nJ3,Carol,Pro,2", 2);
  assertHeat(result.heats[0], 1, "Open", {"Alice", "Bob"});
  assertHeat(result.heats[1], 2, "Pro", {"Carol"});
}

void test_csv_quoted_fields() {
  const HeatListParseResult result = parseOk(
      "JDLid,name,class,heat\n"
      "J1,\"Smith, John\",\"Open, A\",1\n"
      "J2,\"The \"\"Rocket\"\" Sato\",Open,\"1\"\n"
      "J3,\"  padded  \" ,Open,1\n",
      1);
  assertHeat(result.heats[0], 1, "Open, A", {"Smith, John", "The \"Rocket\" Sato", "  padded  "});
}

void test_csv_out_of_order_heats_are_sorted() {
  const HeatListParseResult result = parseOk(
      "JDLid,name,class,heat\n"
      "J1,Carol,Pro,3\n"
      "J2,Alice,Novice,1\n"
      "J3,Bob,Open,2\n"
      "J4,Dave,Pro,3\n"
      "J5,Erin,Novice,1\n",
      3);
  assertHeat(result.heats[0], 1, "Novice", {"Alice", "Erin"});
  assertHeat(result.heats[1], 2, "Open", {"Bob"});
  assertHeat(result.heats[2], 3, "Pro", {"Carol", "Dave"});
}

void test_csv_class_name_comes_from_first_row_of_heat() {
  const HeatListParseResult result = parseOk("J1,Alice,Open,1\nJ2,Bob,Pro,1\n", 1);
  assertHeat(result.heats[0], 1, "Open", {"Alice", "Bob"});
}

void test_csv_blank_lines_and_padding() {
  const HeatListParseResult result = parseOk(
      "\n"
      "  \t\n"
      "JDLid,name,class,heat\n"
      "\n"
      "  J1 ,  Alice Smith , Open\t, 1 \n"
      "\r\n"
      "J2,Bob,Open,01\n"
      "\n",
      1);
  assertHeat(result.heats[0], 1, "Open", {"Alice Smith", "Bob"});
}

void test_csv_extra_fields_ignored() {
  const HeatListParseResult result = parseOk("JDLid,name,class,heat,note\nJ1,Alice,Open,1,seeded,x\nJ2,Bob,Open,1,\n", 1);
  assertHeat(result.heats[0], 1, "Open", {"Alice", "Bob"});
}

void test_csv_rejects_html_error_page() {
  assertRejected(
      "<!DOCTYPE html>\n"
      "<html><head><title>Error</title>\n"
      "<style>body {font-family: Arial, Helvetica, sans-serif, monospace}</style></head>\n"
      "<body><div>Script function not found: doGet</div></body></html>\n",
      "line 1:");
}

void test_csv_rejects_single_line_html_with_commas() {
  // Four comma-separated chunks make this line pass for a header; it must still not be
  // accepted as a heat list.
  assertRejected("<html><body style=\"font: 1px a, b, c, d\">Error</body></html>", "no heat rows");
}

void test_csv_rejects_short_row_with_line_number() {
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,1\nJ2,Bob,Open\nJ3,Carol,Open,1\n", "line 3:");
}

void test_csv_line_number_counts_blank_lines() {
  assertRejected("JDLid,name,class,heat\n\nJ1,Alice,Open,1\n\nJ2,Bob\n", "line 5:");
}

void test_csv_rejects_non_numeric_heat() {
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,1\nJ2,Bob,Open,final\n", "line 3:");
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,1.5\n", "line 2:");
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,2x\n", "line 2:");
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,\n", "line 2:");
}

void test_csv_rejects_zero_heat() {
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,0\n", "line 2:");
}

void test_csv_rejects_negative_heat() {
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,-1\n", "line 2:");
}

void test_csv_rejects_out_of_range_heat() {
  assertRejected("JDLid,name,class,heat\nJ1,Alice,Open,99999999999\n", "line 2:");
}

void test_csv_numeric_first_row_is_data_not_header() {
  // An integer in the 4th field means "no header", so a bad value there is an error
  // rather than a silently skipped line.
  assertRejected("J1,Alice,Open,0\nJ2,Bob,Open,1\n", "line 1:");
  assertRejected("J1,Alice,Open,99999999999\nJ2,Bob,Open,1\n", "line 1:");
}

void test_csv_rejects_second_header_line() {
  assertRejected("JDLid,name,class,heat\nJDLid,name,class,heat\nJ1,Alice,Open,1\n", "line 2:");
}

void test_csv_rejects_unterminated_quote() {
  assertRejected("JDLid,name,class,heat\nJ1,\"Alice,Open,1\n", "line 2:");
}

void test_csv_rejects_text_after_closing_quote() {
  assertRejected("JDLid,name,class,heat\nJ1,\"Alice\"x,Open,1\n", "line 2:");
}

void test_csv_rejects_empty_input() {
  assertRejected("", "no heat rows");
  assertRejected("\n\r\n  \n", "no heat rows");
  assertRejected("\xEF\xBB\xBF", "no heat rows");
}

void test_csv_rejects_header_only() {
  assertRejected("JDLid,name,class,heat\n", "no heat rows");
}

}  // namespace

void runHeatListTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_csv_real_data_excerpt);
  RUN_TEST(test_csv_crlf_line_endings);
  RUN_TEST(test_csv_utf8_bom);
  RUN_TEST(test_csv_bom_without_header);
  RUN_TEST(test_csv_without_header);
  RUN_TEST(test_csv_quoted_fields);
  RUN_TEST(test_csv_out_of_order_heats_are_sorted);
  RUN_TEST(test_csv_class_name_comes_from_first_row_of_heat);
  RUN_TEST(test_csv_blank_lines_and_padding);
  RUN_TEST(test_csv_extra_fields_ignored);
  RUN_TEST(test_csv_rejects_html_error_page);
  RUN_TEST(test_csv_rejects_single_line_html_with_commas);
  RUN_TEST(test_csv_rejects_short_row_with_line_number);
  RUN_TEST(test_csv_line_number_counts_blank_lines);
  RUN_TEST(test_csv_rejects_non_numeric_heat);
  RUN_TEST(test_csv_rejects_zero_heat);
  RUN_TEST(test_csv_rejects_negative_heat);
  RUN_TEST(test_csv_rejects_out_of_range_heat);
  RUN_TEST(test_csv_numeric_first_row_is_data_not_header);
  RUN_TEST(test_csv_rejects_second_header_line);
  RUN_TEST(test_csv_rejects_unterminated_quote);
  RUN_TEST(test_csv_rejects_text_after_closing_quote);
  RUN_TEST(test_csv_rejects_empty_input);
  RUN_TEST(test_csv_rejects_header_only);
}

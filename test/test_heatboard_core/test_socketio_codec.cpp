#include <optional>
#include <string>
#include <string_view>

#include <unity.h>

#include "heatboard/socketio_codec.h"
#include "test_suites.h"

using heatboard::decodeSioPacket;
using heatboard::encodeSioEvent;
using heatboard::parseSioIntArg;
using heatboard::SioPacket;
using heatboard::SioPacketType;

namespace {

void assertType(SioPacketType expected, std::string_view frame) {
  const SioPacket packet = decodeSioPacket(frame);
  const std::string label(frame);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(packet.type), label.c_str());
  // Name and arguments are documented as "Event only".
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", packet.eventName.c_str(), label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", packet.argsJson.c_str(), label.c_str());
}

void assertEvent(std::string_view frame, const char* eventName, const char* argsJson) {
  const SioPacket packet = decodeSioPacket(frame);
  const std::string label(frame);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SioPacketType::Event), static_cast<int>(packet.type), label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(eventName, packet.eventName.c_str(), label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(argsJson, packet.argsJson.c_str(), label.c_str());
}

void assertInt(int expected, std::string_view argsJson) {
  const std::optional<int> value = parseSioIntArg(argsJson);
  const std::string label(argsJson);
  TEST_ASSERT_TRUE_MESSAGE(value.has_value(), label.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(expected, *value, label.c_str());
}

void assertNoInt(std::string_view argsJson) {
  const std::string label(argsJson);
  TEST_ASSERT_FALSE_MESSAGE(parseSioIntArg(argsJson).has_value(), label.c_str());
}

void test_sio_engine_open_handshake() {
  assertType(SioPacketType::EngineOpen, R"(0{"sid":"abc","upgrades":[],"pingInterval":25000,"pingTimeout":20000})");
}

void test_sio_engine_close() {
  assertType(SioPacketType::EngineClose, "1");
}

void test_sio_engine_ping() {
  assertType(SioPacketType::EnginePing, "2");
  assertType(SioPacketType::EnginePing, "2probe");
}

void test_sio_engine_pong() {
  assertType(SioPacketType::EnginePong, "3");
}

void test_sio_namespace_connected() {
  assertType(SioPacketType::NamespaceConnected, "40");
  assertType(SioPacketType::NamespaceConnected, R"(40{"sid":"xyz"})");
}

void test_sio_namespace_disconnected() {
  assertType(SioPacketType::NamespaceDisconnected, "41");
}

void test_sio_connect_error() {
  assertType(SioPacketType::ConnectError, R"(44{"message":"Not authorized"})");
}

void test_sio_event_with_int_arg() {
  assertEvent(R"(42["current_heat",5])", "current_heat", "5");
}

void test_sio_event_with_nested_array_arg() {
  assertEvent(R"(42["heat_list",[["a","b"]]])", "heat_list", R"([["a","b"]])");
}

void test_sio_event_with_several_args() {
  assertEvent(R"(42["lap",{"pilot":"a]b","n":2},[1,2]])", "lap", R"({"pilot":"a]b","n":2},[1,2])");
}

void test_sio_event_without_arg() {
  assertEvent(R"(42["start_heat"])", "start_heat", "");
}

void test_sio_event_with_ack_id() {
  assertEvent(R"(4217["current_heat",5])", "current_heat", "5");
  assertEvent(R"(420["start_heat"])", "start_heat", "");
}

void test_sio_event_with_escaped_name() {
  assertEvent(R"(42["say \"hi\"",1])", R"(say "hi")", "1");
  assertEvent(R"(42["back\\slash"])", R"(back\slash)", "");
  assertEvent(R"(42["a,b]",1])", "a,b]", "1");
}

void test_sio_event_with_whitespace() {
  assertEvent(R"(42[ "current_heat" , 5 ])", "current_heat", "5");
}

void test_sio_malformed_frames_are_unknown() {
  for (const std::string_view frame : {
           std::string_view(""),
           std::string_view("42"),
           std::string_view("42["),
           std::string_view("42[]"),
           std::string_view("42[5]"),
           std::string_view(R"(42[["nested"]])"),
           std::string_view(R"(42["unterminated])"),
           std::string_view(R"(42["trailing\"])"),
           std::string_view(R"(42["name",])"),
           std::string_view(R"(42["name" 5])"),
           std::string_view(R"(42["name",5)"),
           std::string_view(R"(42"name")"),
           std::string_view(R"(42["bad\qescape"])"),
       }) {
    assertType(SioPacketType::Unknown, frame);
  }
}

void test_sio_non_default_namespace_is_unknown() {
  assertType(SioPacketType::Unknown, R"(42/admin,["x"])");
  assertType(SioPacketType::Unknown, "40/admin,");
  assertType(SioPacketType::Unknown, R"(40/admin,{"sid":"xyz"})");
  assertType(SioPacketType::Unknown, "41/admin,");
  assertType(SioPacketType::Unknown, R"(44/admin,{"message":"no"})");
}

void test_sio_unsupported_packets_are_unknown() {
  for (const std::string_view frame : {
           std::string_view("0"),               // open without a handshake object
           std::string_view("1x"),              // close carries no data
           std::string_view("3probe"),          // only sent during transport upgrades
           std::string_view("4"),               // message without a Socket.IO packet
           std::string_view(R"(43[1,"ok"])"),   // ack
           std::string_view(R"(45["bin"])"),    // binary event
           std::string_view("5"),               // upgrade
           std::string_view("6"),               // noop
           std::string_view("hello"),
           std::string_view("<html>"),
       }) {
    assertType(SioPacketType::Unknown, frame);
  }
}

void test_sio_client_frame_constants() {
  TEST_ASSERT_TRUE(heatboard::kSioNamespaceConnect == "40");
  TEST_ASSERT_TRUE(heatboard::kSioEnginePong == "3");
}

void test_sio_encode_event_without_arg() {
  TEST_ASSERT_EQUAL_STRING(R"(42["start_heat"])", encodeSioEvent("start_heat").c_str());
}

void test_sio_encode_event_with_int_arg() {
  TEST_ASSERT_EQUAL_STRING(R"(42["set_current_heat",5])", encodeSioEvent("set_current_heat", 5).c_str());
  TEST_ASSERT_EQUAL_STRING(R"(42["set_current_heat",0])", encodeSioEvent("set_current_heat", 0).c_str());
  TEST_ASSERT_EQUAL_STRING(R"(42["set_current_heat",-12])", encodeSioEvent("set_current_heat", -12).c_str());
}

void test_sio_encode_escapes_name() {
  TEST_ASSERT_EQUAL_STRING(R"(42["a\"b\\c"])", encodeSioEvent(R"(a"b\c)").c_str());
  TEST_ASSERT_EQUAL_STRING(R"(42["a\"b\\c",7])", encodeSioEvent(R"(a"b\c)", 7).c_str());
}

void test_sio_encoded_events_decode_back() {
  assertEvent(encodeSioEvent("start_heat"), "start_heat", "");
  assertEvent(encodeSioEvent("set_current_heat", 38), "set_current_heat", "38");
  assertEvent(encodeSioEvent(R"(a"b\c)", 7), R"(a"b\c)", "7");
}

void test_sio_int_arg_accepted_forms() {
  assertInt(5, "5");
  assertInt(38, "38");
  assertInt(0, "0");
  assertInt(-3, "-3");
  assertInt(5, "\"5\"");
  assertInt(5, "  5\t\r\n");
  assertInt(5, " \"5\" ");
  assertInt(2147483647, "2147483647");
}

void test_sio_int_arg_rejected_forms() {
  for (const std::string_view argsJson : {
           std::string_view(""),
           std::string_view("   "),
           std::string_view("5.0"),
           std::string_view("5e2"),
           std::string_view("[5]"),
           std::string_view("5,6"),
           std::string_view("5x"),
           std::string_view("x5"),
           std::string_view("+5"),
           std::string_view("5 6"),
           std::string_view("-"),
           std::string_view("\"\""),
           std::string_view("\"5"),
           std::string_view("\" 5\""),
           std::string_view("\"5.0\""),
           std::string_view("null"),
           std::string_view("true"),
           std::string_view("{\"n\":5}"),
           std::string_view("2147483648"),
           std::string_view("99999999999999999999"),
       }) {
    assertNoInt(argsJson);
  }
}

void test_sio_int_arg_from_decoded_event() {
  const SioPacket packet = decodeSioPacket(R"(42["current_heat",12])");
  assertInt(12, packet.argsJson);
}

}  // namespace

void runSocketIoCodecTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_sio_engine_open_handshake);
  RUN_TEST(test_sio_engine_close);
  RUN_TEST(test_sio_engine_ping);
  RUN_TEST(test_sio_engine_pong);
  RUN_TEST(test_sio_namespace_connected);
  RUN_TEST(test_sio_namespace_disconnected);
  RUN_TEST(test_sio_connect_error);
  RUN_TEST(test_sio_event_with_int_arg);
  RUN_TEST(test_sio_event_with_nested_array_arg);
  RUN_TEST(test_sio_event_with_several_args);
  RUN_TEST(test_sio_event_without_arg);
  RUN_TEST(test_sio_event_with_ack_id);
  RUN_TEST(test_sio_event_with_escaped_name);
  RUN_TEST(test_sio_event_with_whitespace);
  RUN_TEST(test_sio_malformed_frames_are_unknown);
  RUN_TEST(test_sio_non_default_namespace_is_unknown);
  RUN_TEST(test_sio_unsupported_packets_are_unknown);
  RUN_TEST(test_sio_client_frame_constants);
  RUN_TEST(test_sio_encode_event_without_arg);
  RUN_TEST(test_sio_encode_event_with_int_arg);
  RUN_TEST(test_sio_encode_escapes_name);
  RUN_TEST(test_sio_encoded_events_decode_back);
  RUN_TEST(test_sio_int_arg_accepted_forms);
  RUN_TEST(test_sio_int_arg_rejected_forms);
  RUN_TEST(test_sio_int_arg_from_decoded_event);
}

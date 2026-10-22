#include <unity.h>

#include "test_suites.h"

void setUp(void) {}
void tearDown(void) {}

int main() {
  UNITY_BEGIN();
  runHeatListTests();
  runHeatWindowTests();
  runSocketIoCodecTests();
  runServerAddressTests();
  runServerHeartbeatTests();
  runOrientationTests();
  runGestureControlTests();
  runWifiProfileTests();
  return UNITY_END();
}

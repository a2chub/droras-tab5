// Each suite runs its cases with RUN_TEST; test_main.cpp calls them between UNITY_BEGIN()
// and UNITY_END(). A suite starts with UnitySetTestFile(__FILE__) because UNITY_BEGIN() only
// records test_main.cpp, which would make every result line point at the wrong file.
#pragma once

void runHeatListTests();
void runHeatWindowTests();
void runSocketIoCodecTests();
void runServerAddressTests();

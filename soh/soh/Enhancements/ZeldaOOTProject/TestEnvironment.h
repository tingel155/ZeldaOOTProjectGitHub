#pragma once

// Test environment for playtesting mods: skips the boot sequence and drops an adult, fully equipped Link
// straight into a configurable scene. Configured via the "gZOP.TestEnv.*" CVars in shipofharkinian.json:
//   Enabled, Adult, NayrusLoveActive, DayTime, Entrance, DoubleDefense, Enemies

#define CVAR_ZOP_TESTENV(var) "gZOP.TestEnv." var

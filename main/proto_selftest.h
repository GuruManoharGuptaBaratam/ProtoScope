/*
 * ProtoScope TEMPORARY hardware self-test.
 *
 * Adds the console command `selftest`. Every result it prints comes from a real
 * transfer on the real pins: nothing is simulated, and a test that cannot prove
 * a path prints FAIL or SKIP with the reason instead of a number.
 *
 * Remove after bring-up: delete main/proto_selftest.c and main/proto_selftest.h,
 * remove the two lines marked "SELFTEST" in main/app_main.cpp and restore
 * main/CMakeLists.txt.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the `selftest` console command. Call after proto_console_start(). */
esp_err_t proto_selftest_register(void);

#ifdef __cplusplus
}
#endif

// SPDX-License-Identifier: MIT
// X515EP control extension. Original AsusSMC copyright/license remains in LICENSE.md.
// Fixed-width user-client ABI, independent of IOKit. All profiles are normalized:
// 0 balanced, 1 performance, 2 silent. Firmware VivoBook values swap 1 and 2.
#ifndef ASUS_CONTROL_ABI_HPP
#define ASUS_CONTROL_ABI_HPP
#include <stdint.h>
namespace AsusControl {
constexpr uint32_t abiVersion = 5;
constexpr unsigned stateCount = 36;
enum Selector : uint32_t { getState = 0, set = 1, getKeyActions = 2 };
constexpr uint32_t firstKeySetting = 0x101, lastKeySetting = 0x10c;
enum Setting : uint32_t { profile = 1, charge = 5, fnlock = 9, lowPowerConfig = 10, fnHotkeysValue = 11 };
enum Capability : uint32_t {
    capProfile = 1, capCharge = 2, capFnlock = 4,
    capHotkeys = 64, capCpuFan = 128, capGpuFan = 256, capKeyBindings = 1024
};
enum Flag : uint32_t { onAC = 1, profileDeferred = 4, powerUnknown = 8, lowPowerOverride = 16 };
enum StateWord : unsigned {
    version = 0, flags = 1, actualProfile = 2, chargeRequest = 3,
    cpuFanRPM = 4, gpuFanRPM = 5, lowPowerConfiguration = 6,
    // Word 6: enabled bit0, macOS low-power active bit1, normalized override profile bits8..9.
    fnHotkeysFirmwareValue = 7, latestKeyAction = 8, keyCaptureMask = 9,
    // 10..26: reserved legacy prefix; no PPT, manual curves or lighting support.
    capabilities = 27, fnlockRequest = 28, profileDevice = 29,
    lastKey = 30, keySequence = 31,
    desiredProfile = 32, powerSequence = 33, performanceDropSequence = 34,
    controlError = 35
};
constexpr uint32_t unknown = 0xffffffff;
constexpr int minCharge = 50, maxCharge = 100;
}
#endif

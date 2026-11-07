// SPDX-License-Identifier: MIT
#ifndef ASUS_KEY_POLICY_HPP
#define ASUS_KEY_POLICY_HPP
#include <stdint.h>
namespace AsusControl {
enum KeyAction : uint32_t {
    keyNone = 0, keyMute, keyVolumeDown, keyVolumeUp, keyBrightnessDown, keyBrightnessUp,
    keyTouchpad, keyDisplays, keyLockScreen, keyCameraPrivacy, keyScreenshot, keySettings,
    keyNextProfile, keyBalanced, keyPerformance, keySilent
};
constexpr uint32_t defaultBindings[12] = { 1, 2, 3, 4, 5, 6, 0, 7, 8, 9, 10, 11 };
constexpr uint32_t nativeKeyMask = 0xb20; // F6/F9/F10/F12 have confirmed native WMI paths
constexpr uint32_t bridgeKeyMask = 0xbbf; // EC query bridge: no known event for F7 or F11
inline unsigned functionKey(uint32_t code) {
    if (code >= 0xf101 && code <= 0xf10c) return code - 0xf100;
    switch (code) {
        case 0x32: return 1; case 0x31: return 2; case 0x30: return 3;
        case 0x6b: return 6; case 0x33: case 0x35: return 9;
        case 0x85: return 10; case 0x86: return 12;
        default: return 0;
    }
}
inline bool validKeyAction(uint32_t action) { return action <= keySilent; }
inline uint32_t fnFirmwareValue(int logicalLock, uint32_t hotkeysValue) {
    return logicalLock == 0 ? hotkeysValue : 1 - hotkeysValue;
}
struct KeyActionEvent { uint32_t sequence, key, action, reserved; };
struct KeyActionQueue {
    KeyActionEvent events[64] {};
    uint32_t sequence = 0;
    unsigned count = 0, head = 0;
    void push(unsigned key, uint32_t action) {
        events[head] = { ++sequence, key, action, 0 };
        head = (head + 1) % 64;
        if (count < 64) ++count;
    }
    unsigned copyAfter(uint32_t after, KeyActionEvent out[64]) const {
        unsigned copied = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto &event = events[(head + 64 - count + i) % 64];
            if ((int32_t)(event.sequence - after) > 0) out[copied++] = event;
        }
        return copied;
    }
};
}
#endif

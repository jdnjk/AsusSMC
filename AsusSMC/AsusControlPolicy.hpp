// SPDX-License-Identifier: MIT
// Pure host-testable policy. No hardware access, OS defaults, or implicit profile writes.
#ifndef ASUS_CONTROL_POLICY_HPP
#define ASUS_CONTROL_POLICY_HPP
#include "AsusControlABI.hpp"
namespace AsusControl {
constexpr uint32_t vivoProfileDevice = 0x00110019;
constexpr uint32_t chargeDevice = 0x00120057;
constexpr uint32_t fnlockDevice = 0x00100023;
constexpr uint32_t presence = 0x00010000;
inline bool present(uint32_t raw) { return raw != 0xfffffffe && raw != unknown && (raw & presence); }
inline bool validProfile(int value) { return value >= 0 && value <= 2; }
inline bool validCharge(int value) { return value >= minCharge && value <= maxCharge; }
inline int vivoValue(int normalized) { return !validProfile(normalized) ? -1 : normalized == 0 ? 0 : 3 - normalized; }
inline int vivoReadback(uint32_t raw) { return present(raw) ? vivoValue((int)(raw & 0xffff)) : -1; }
inline bool fnlockAvailable(uint32_t raw) { return present(raw) && !(raw & 1); }
inline bool validLowPowerConfig(uint32_t packed) { return !(packed & ~0x303u) && ((packed >> 8) & 3) <= 2; }

struct ProfilePolicy {
    int desired = -1;                 // intent; NEVER replaced by the battery fallback
    int actual = -1;                  // firmware readback; unknown is not reported as success
    bool knownPower = false, connected = false;
    bool everKnownPower = false, lastKnownAC = false;
    uint32_t powerChanges = 0, performanceDrops = 0;
    uint32_t lowPower = 2u << 8;       // default OFF; Silent is merely the suggested rule
    int overrideResume = -1;          // transient baseline, not a saved user preference
    bool overriding() const { return (lowPower & 3) == 3; }
    int effective() const { return overriding() ? (int)((lowPower >> 8) & 3) : validProfile(desired) ? desired : overrideResume; }
    bool configureLowPower(uint32_t packed) {
        if (!validLowPowerConfig(packed)) return false;
        bool entering = (packed & 3) == 3 && !overriding();
        if (entering && !validProfile(desired)) {
            if (!validProfile(actual)) return false; // cannot promise restore without a known baseline
            overrideResume = actual;
        }
        lowPower = packed;
        return true;
    }

    bool request(int value) {
        if (!validProfile(value)) return false;
        desired = value;              // performance is valid even on battery
        overrideResume = -1;
        return true;
    }
    void observePower(bool known, bool ac) {
        knownPower = known;
        connected = known && ac;      // fail-safe target is balanced if AC status cannot be read
        if (!known) return;           // preserve last KNOWN source through a transient read failure
        if (everKnownPower && lastKnownAC != ac) {
            ++powerChanges;
            if (lastKnownAC && !ac && effective() == 1) ++performanceDrops;
        }
        everKnownPower = true;
        lastKnownAC = ac;
    }
    void observeProfile(int value) {
        actual = value;
        // Retry acquiring the inherited runtime intent if the first boot read failed.
        // Never adopt the temporary low-power preset or overwrite an explicit/pending intent.
        if (validProfile(value) && !validProfile(desired) && !overriding() && !validProfile(overrideResume)) desired = value;
    }
    int target() const { int mode = effective(); return mode == 1 && !connected ? 0 : mode; }
    bool deferred() const { return effective() == 1 && !connected; }
    void didApplyTarget() {
        if (!overriding() && !validProfile(desired) && actual == overrideResume) overrideResume = -1;
    }
    int next() const { return validProfile(desired) ? (desired + 1) % 3 : 0; }
};
}
#endif

// SPDX-License-Identifier: MIT
// Host-only tests of the production AsusSMC policy. Never calls WMI or loads a driver.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include "../AsusSMC/AsusControlPolicy.hpp"

int main() {
    using namespace AsusControl;
    static_assert(abiVersion == 5 && stateCount == 36, "ABI size");
    static_assert(actualProfile == 2 && lowPowerConfiguration == 6 && capabilities == 27 && fnlockRequest == 28, "prefix");
    static_assert(desiredProfile == 32 && powerSequence == 33 && performanceDropSequence == 34 && controlError == 35, "tail");
    static_assert(profile == 1 && charge == 5 && fnlock == 9 && lowPowerConfig == 10, "SET contract");
    assert(validCharge(50) && validCharge(51) && validCharge(79) && validCharge(99) && validCharge(100));
    assert(!validCharge(49) && !validCharge(101) && !validCharge(-1));
    assert(vivoValue(0) == 0 && vivoValue(1) == 2 && vivoValue(2) == 1);
    for (int i = 0; i <= 2; ++i) assert(vivoReadback(presence | (uint32_t)vivoValue(i)) == i);
    assert(vivoReadback(0xfffffffe) == -1 && vivoReadback(unknown) == -1 && vivoReadback(2) == -1);
    assert(fnlockAvailable(0x10000) && !fnlockAvailable(0x10001));

    ProfilePolicy p;
    assert(p.desired == -1 && p.target() == -1 && !p.overriding()); // no default write
    p.observePower(true, false);
    assert(p.performanceDrops == 0 && p.powerChanges == 0);       // no stale startup notification
    assert(p.request(1));                                       // performance is selectable on battery!
    assert(p.desired == 1 && p.target() == 0 && p.deferred());
    p.actual = 0;
    p.observePower(true, true);
    assert(p.target() == 1 && p.desired == 1 && !p.deferred());    // reconnect restores same intent
    assert(p.powerChanges == 1 && p.performanceDrops == 0);
    p.actual = 1;
    p.observePower(true, false);
    assert(p.target() == 0 && p.desired == 1 && p.performanceDrops == 1);
    p.actual = -1; // readback/write failure does not erase the desired mode or count each retry as a new unplug
    p.observePower(true, false);
    assert(p.performanceDrops == 1 && p.desired == 1 && p.target() == 0);
    p.observePower(true, true);
    assert(p.target() == 1);
    p.observePower(false, false);                               // unknown source fails safe, preserves last KNOWN source
    assert(p.target() == 0 && p.performanceDrops == 1);
    p.observePower(true, false);
    assert(p.performanceDrops == 2);
    assert(!p.request(3) && !p.request(-1) && p.desired == 1);
    p.request(2);
    assert(p.target() == 2);
    p.observePower(true, true); p.observePower(true, false);
    assert(p.performanceDrops == 2);                            // silent unplug must not say performance was reduced

    p.request(1); p.observePower(true, true);
    assert(p.configureLowPower((2u << 8) | 1));                  // rule enabled but macOS LPM inactive
    assert(p.target() == 1 && !p.overriding());
    assert(p.configureLowPower((2u << 8) | 3));                  // LPM -> Silent, retain normal Performance desire
    assert(p.overriding() && p.target() == 2 && p.desired == 1);
    p.observePower(true, false);
    assert(p.target() == 2 && !p.deferred() && p.performanceDrops == 2);
    assert(p.configureLowPower((2u << 8) | 1));                  // leave LPM, still on battery -> Balanced
    assert(p.target() == 0 && p.desired == 1 && p.deferred());
    p.observePower(true, true);
    assert(p.target() == 1);                                    // then AC -> Performance
    assert(p.configureLowPower((1u << 8) | 3));                  // LPM rule itself may request Performance
    p.observePower(true, false);
    assert(p.target() == 0 && p.desired == 1 && p.performanceDrops == 3);
    p.observePower(true, true);
    assert(p.target() == 1);
    assert(p.configureLowPower(0));                             // disabling restores regular intent
    assert(p.target() == 1 && !p.overriding());
    assert(!validLowPowerConfig(0x300) && !validLowPowerConfig(0x400) && !validLowPowerConfig(4));
    assert(!p.configureLowPower(0x300) && p.lowPower == 0);

    ProfilePolicy baseline;
    baseline.observePower(true, true);
    assert(!baseline.configureLowPower((2u << 8) | 3));           // unknown baseline cannot promise safe restore
    baseline.actual = 1;
    assert(baseline.configureLowPower((2u << 8) | 3));
    assert(baseline.desired == -1 && baseline.target() == 2);
    baseline.actual = 2;
    assert(baseline.configureLowPower(0));
    baseline.observePower(true, false);
    baseline.actual = 0; baseline.didApplyTarget();
    assert(baseline.target() == 0 && baseline.overrideResume == 1); // don't forget resume while battery-deferred
    baseline.observePower(true, true);
    assert(baseline.target() == 1);
    baseline.actual = 1; baseline.didApplyTarget();
    assert(baseline.target() == -1);                             // release transient baseline after restoration

    p.performanceDrops = UINT32_MAX; p.powerChanges = UINT32_MAX;
    p.observePower(true, false);
    assert(p.performanceDrops == 0 && p.powerChanges == 0);       // defined unsigned counter wrap
    ProfilePolicy delayedBoot;
    delayedBoot.observePower(true, false);
    delayedBoot.observeProfile(-1);
    assert(delayedBoot.target() == -1);
    delayedBoot.observeProfile(1); // first successful read can arrive AFTER startup
    assert(delayedBoot.desired == 1 && delayedBoot.target() == 0);
    delayedBoot.observePower(true, true);
    assert(delayedBoot.target() == 1);
    std::puts("AsusSMC X515EP control policy tests passed");
}

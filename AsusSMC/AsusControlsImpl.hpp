// SPDX-License-Identifier: MIT
// Included only by AsusSMC.cpp. X515EP WMI controls share the existing ATKD/event owner.
#ifndef ASUS_CONTROLS_IMPL_HPP
#define ASUS_CONTROLS_IMPL_HPP

void AsusSMC::observeControlPowerLocked() {
    UInt32 ac = 0;
    // Verified against the local X515EP.316 DSDT. An absent/failed method is NOT proof of AC power.
    IOReturn result = controlACDevice ? controlACDevice->evaluateInteger("_PSR", &ac) :
        atkDevice->evaluateInteger("\\_SB.PC00.LPCB.EC0.AC0._PSR", &ac);
    bool known = result == kIOReturnSuccess && ac <= 1;
    profilePolicy.observePower(known, ac == 1);
}

IOReturn AsusSMC::controlPowerNotification(void *target, void *, UInt32 type, IOService *, void *argument, vm_size_t) {
    auto self = static_cast<AsusSMC *>(target);
    // X515EP signals AC0 with Notify(0x80); do the ACPI work on our workloop, not in this callback.
    if (type == kIOACPIMessageDeviceNotification && argument && *(UInt32 *)argument == 0x80 && self->poller)
        self->poller->setTimeoutMS(1);
    return kIOReturnSuccess;
}

void AsusSMC::initControls() {
    using namespace AsusControl;
    auto matching = IOService::nameMatching("ACPI0003");
    if (matching) {
        auto it = IOService::getMatchingServices(matching);
        matching->release();
        if (it) {
            controlACDevice = OSDynamicCast(IOACPIPlatformDevice, it->getNextObject());
            if (controlACDevice) {
                controlACDevice->retain();
                controlACNotify = controlACDevice->registerInterest(gIOGeneralInterest, controlPowerNotification, this);
            }
            it->release();
        }
    }
    if (wmi_dev_is_present(vivoProfileDevice)) controlCaps |= capProfile;
    if (isBatteryRSOCAvailable) controlCaps |= capCharge;
    int raw = wmi_get_devstate(fnlockDevice);
    if (raw >= 0 && fnlockAvailable((uint32_t)raw)) controlCaps |= capFnlock;
    if (wmi_dev_is_present(ASUS_WMI_DEVID_CPU_FAN_CTRL)) controlCaps |= capCpuFan;
    if (wmi_dev_is_present(0x00110014)) controlCaps |= capGpuFan;
    if (atkInitialized && atkDevice->validateObject("_WED") == kIOReturnSuccess) controlCaps |= capHotkeys | capKeyBindings;
    UInt32 bridged = 0;
    if (atkDevice->evaluateInteger("XKBD", &bridged) == kIOReturnSuccess) capturedKeys |= bridged & bridgeKeyMask;
    // User default: logical unlocked=0 means direct shortcut keys. Firmware polarity is configurable.
    if (controlCaps & capFnlock) setFnlockLocked(0);
    observeControlPowerLocked();
    // Keep a KNOWN firmware mode as transient runtime intent, without saving it or imposing a default.
    // This also allows an inherited performance mode to fall back on battery and resume on AC.
    if (controlCaps & capProfile) {
        profilePolicy.observeProfile(vivoReadback((uint32_t)wmi_get_devstate(vivoProfileDevice)));
        if (validProfile(profilePolicy.actual)) profilePolicy.request(profilePolicy.actual);
    }
    setProperty("AsusControl-ABI", (unsigned long long)abiVersion, 32);
    setProperty("AsusControl-Capabilities", (unsigned long long)controlCaps, 32);
    setProperty("AsusControl-Variant", "X515EP local extension (unverified build)");
}

void AsusSMC::reconcileProfileLocked() {
    using namespace AsusControl;
    if (!(controlCaps & capProfile)) return;
    profilePolicy.observeProfile(vivoReadback((uint32_t)wmi_get_devstate(vivoProfileDevice)));
    int target = profilePolicy.target();
    if (!validProfile(target)) return; // no explicit user preference, no profile write
    if (profilePolicy.actual == target) { lastControlError = kIOReturnSuccess; profilePolicy.didApplyTarget(); return; }
    int status = wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, vivoProfileDevice, vivoValue(target));
    // On X515EP DEVS returns One; Zero is not success. Readback is FANF, not a measured fan RPM.
    profilePolicy.observeProfile(vivoReadback((uint32_t)wmi_get_devstate(vivoProfileDevice)));
    lastControlError = status == 1 && profilePolicy.actual == target ? kIOReturnSuccess : kIOReturnIOError;
    if (lastControlError == kIOReturnSuccess) profilePolicy.didApplyTarget();
}

void AsusSMC::pollControls() {
    if (!x515epControls || !controlLock) return;
    IOLockLock(controlLock);
    observeControlPowerLocked();
    reconcileProfileLocked(); // retries failed transitions and restores performance after reconnect/wake
    if (controlCaps & AsusControl::capGpuFan) {
        uint32_t raw = (uint32_t)wmi_get_devstate(0x00110014);
        controlGpuFan = AsusControl::present(raw) ? (raw & 0xffff) * 100 : AsusControl::unknown;
    }
    IOLockUnlock(controlLock);
}

IOReturn AsusSMC::setFnlockLocked(int value) {
    using namespace AsusControl;
    if (!(controlCaps & capFnlock)) return kIOReturnUnsupported;
    if (value != 0 && value != 1) return kIOReturnBadArgument;
    int status = wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, fnlockDevice, fnFirmwareValue(value, hotkeysFirmwareValue));
    if (status != 1) return kIOReturnIOError;
    fnlockLastRequest = value; // DSTS bit 0 is BIOS-disabled; it is NOT a readback of this mode
    return kIOReturnSuccess;
}

IOReturn AsusSMC::controlSet(uint32_t what, uint32_t value) {
    using namespace AsusControl;
    if (!x515epControls || !controlLock) return kIOReturnUnsupported;
    IOLockLock(controlLock);
    IOReturn result = kIOReturnUnsupported;
    if (what >= firstKeySetting && what <= lastKeySetting) {
        unsigned index = what - firstKeySetting;
        if ((controlCaps & capKeyBindings) && (capturedKeys & (1u << index))) {
            if (validKeyAction(value)) { keyBindings[index] = value; result = kIOReturnSuccess; }
            else result = kIOReturnBadArgument;
        }
        IOLockUnlock(controlLock);
        return result;
    }
    switch (what) {
        case profile:
            if (!(controlCaps & capProfile)) break;
            if (value > 2) { result = kIOReturnBadArgument; break; }
            observeControlPowerLocked();
            profilePolicy.request((int)value);
            reconcileProfileLocked();
            // This acknowledges INTENT. Actual/readback and any pending error are returned in GetState.
            result = kIOReturnSuccess;
            break;
        case charge:
            if (!(controlCaps & capCharge)) break;
            if (value < minCharge || value > maxCharge) { result = kIOReturnBadArgument; break; }
            result = wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, chargeDevice, value) == 1 ? kIOReturnSuccess : kIOReturnIOError;
            if (result == kIOReturnSuccess) chargeLimitRequest = (int)value;
            break;
        case fnlock:
            if (value > 1) { result = kIOReturnBadArgument; break; }
            result = setFnlockLocked((int)value);
            break;
        case fnHotkeysValue: {
            if (!(controlCaps & capFnlock)) break;
            if (value > 1) { result = kIOReturnBadArgument; break; }
            uint32_t previous = hotkeysFirmwareValue;
            hotkeysFirmwareValue = value;
            result = fnlockLastRequest >= 0 ? setFnlockLocked(fnlockLastRequest) : kIOReturnSuccess;
            if (result != kIOReturnSuccess) hotkeysFirmwareValue = previous;
            break;
        }
        case lowPowerConfig:
            if (!(controlCaps & capProfile)) break;
            if (!validLowPowerConfig(value)) { result = kIOReturnBadArgument; break; }
            observeControlPowerLocked();
            profilePolicy.observeProfile(vivoReadback((uint32_t)wmi_get_devstate(vivoProfileDevice)));
            if (!profilePolicy.configureLowPower(value)) { result = kIOReturnNotReady; break; }
            reconcileProfileLocked();
            result = kIOReturnSuccess; // intent acknowledged; state carries actual/error
            break;
        default: break;
    }
    IOLockUnlock(controlLock);
    return result;
}

IOReturn AsusSMC::controlGetState(uint32_t state[AsusControl::stateCount]) {
    using namespace AsusControl;
    if (!x515epControls || !controlLock) return kIOReturnUnsupported;
    IOLockLock(controlLock);
    // Timer owns power reconciliation. Reads do not change user preferences or execute profile DEVS.
    bzero(state, stateCount * sizeof(uint32_t));
    state[version] = abiVersion;
    state[flags] = (profilePolicy.connected ? onAC : 0) | (profilePolicy.deferred() ? profileDeferred : 0) |
                   (!profilePolicy.knownPower ? powerUnknown : 0) | (profilePolicy.overriding() ? lowPowerOverride : 0);
    state[lowPowerConfiguration] = profilePolicy.lowPower;
    state[actualProfile] = validProfile(profilePolicy.actual) ? (uint32_t)profilePolicy.actual : unknown;
    state[desiredProfile] = validProfile(profilePolicy.desired) ? (uint32_t)profilePolicy.desired : unknown;
    state[chargeRequest] = chargeLimitRequest >= 0 ? (uint32_t)chargeLimitRequest : unknown;
    state[cpuFanRPM] = atomic_load_explicit(&currentFanSpeed, memory_order_acquire);
    state[15] = state[16] = state[18] = state[24] = state[25] = unknown;
    state[capabilities] = controlCaps;
    if (state[cpuFanRPM] == 10000) { state[cpuFanRPM] = 0; state[capabilities] &= ~capCpuFan; }
    state[gpuFanRPM] = controlGpuFan == unknown ? 0 : controlGpuFan;
    if (controlGpuFan == unknown) state[capabilities] &= ~capGpuFan;
    state[fnHotkeysFirmwareValue] = hotkeysFirmwareValue;
    state[latestKeyAction] = keyActions.sequence;
    state[keyCaptureMask] = capturedKeys;
    state[fnlockRequest] = fnlockLastRequest >= 0 ? (uint32_t)fnlockLastRequest : unknown;
    state[profileDevice] = controlCaps & capProfile ? vivoProfileDevice : 0;
    state[lastKey] = controlLastKey;
    state[keySequence] = controlKeySequence;
    state[powerSequence] = profilePolicy.powerChanges;
    state[performanceDropSequence] = profilePolicy.performanceDrops;
    state[controlError] = lastControlError;
    IOLockUnlock(controlLock);
    return kIOReturnSuccess;
}

IOReturn AsusSMC::controlKeyActions(uint32_t after, uint32_t *words, size_t *bytes) {
    if (!x515epControls || !controlLock) return kIOReturnUnsupported;
    if (!words || !bytes || *bytes < (2 + 64 * 4) * sizeof(uint32_t)) return kIOReturnBadArgument;
    AsusControl::KeyActionEvent events[64];
    IOLockLock(controlLock);
    unsigned count = keyActions.copyAfter(after, events);
    words[0] = keyActions.sequence;
    words[1] = count;
    memcpy(words + 2, events, count * sizeof(events[0]));
    IOLockUnlock(controlLock);
    *bytes = (2 + count * 4) * sizeof(uint32_t);
    return kIOReturnSuccess;
}

void AsusSMC::dispatchControlAction(unsigned key, uint32_t action) {
    using namespace AsusControl;
    switch (action) {
        case keyNone: return;
        case keyMute: dispatchCSMRReport(kHIDUsage_Csmr_Mute); return;
        case keyVolumeDown: dispatchCSMRReport(kHIDUsage_Csmr_VolumeDecrement); return;
        case keyVolumeUp: dispatchCSMRReport(kHIDUsage_Csmr_VolumeIncrement); return;
        case keyBrightnessDown: dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown); return;
        case keyBrightnessUp: dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp); return;
        case keyTouchpad: toggleTouchpad(); return;
        default: break;
    }
    IOLockLock(controlLock);
    if (action >= keyNextProfile && action <= keySilent) {
        observeControlPowerLocked();
        profilePolicy.request(action == keyNextProfile ? profilePolicy.next() : (int)action - keyBalanced);
        reconcileProfileLocked();
    } else if (action >= keyDisplays && action <= keySettings) {
        keyActions.push(key, action); // authenticated app executes only these fixed UI actions, never shell strings
    }
    IOLockUnlock(controlLock);
}

bool AsusSMC::handleControlKey(int code) {
    using namespace AsusControl;
    if (!x515epControls || !controlLock) return false;
    IOLockLock(controlLock);
    controlLastKey = (uint32_t)code;
    ++controlKeySequence;
    bool handled = false;
    unsigned key = functionKey((uint32_t)code);
    if (key) {
        uint32_t action = keyBindings[key - 1];
        IOLockUnlock(controlLock); // touchpad dispatch enters the workloop; never do that while holding this lock
        dispatchControlAction(key, action);
        return true;
    } else if (code == 0x4e && (controlCaps & capFnlock)) {
        // Current mode is not readable. The first press establishes On, subsequent presses toggle.
        setFnlockLocked(fnlockLastRequest < 0 ? 1 : 1 - fnlockLastRequest);
        handled = true;
    } else if ((code == 0x9d || code == 0xae) && (controlCaps & capProfile)) {
        observeControlPowerLocked();
        profilePolicy.request(profilePolicy.next()); // includes performance on battery: intent is deferred, not forbidden
        reconcileProfileLocked();
        handled = true;
    }
    IOLockUnlock(controlLock);
    return handled;
}

void AsusSMC::sensorTimer(OSObject *owner, IOTimerEventSource *sender) {
    auto self = OSDynamicCast(AsusSMC, owner);
    if (!self) return;
    if (!self->x515epControls) self->refreshALS(true);
    self->refreshFan();
    self->pollControls();
    // Always reschedule, including machines without ALS and when no VirtualSMC handler has run yet.
    sender->setTimeoutMS(SensorUpdateTimeoutMS);
}
#endif

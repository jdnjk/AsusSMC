//
//  AsusSMC.hpp
//  AsusSMC
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#ifndef _AsusSMC_hpp
#define _AsusSMC_hpp

#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOUserClient.h>
#include "AsusControlPolicy.hpp"
#include "AsusKeyPolicy.hpp"
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "HIDReport.hpp"
#include "HIDUsageTables.h"
#include "VirtualAppleKeyboard.hpp"
#include "KernEventServer.hpp"
#include "KeyImplementations.hpp"

#define ASUS_WMI_METHODID_DSTS         0x53545344
#define ASUS_WMI_METHODID_DEVS         0x53564544
#define ASUS_WMI_METHODID_INIT         0x54494E49
#define ASUS_WMI_DEVID_ALS_ENABLE      0x00050001
#define ASUS_WMI_DEVID_CPU_FAN_CTRL    0x00110013
#define ASUS_WMI_DEVID_RSOC            0x00120057
#define ASUS_WMI_DSTS_PRESENCE_BIT     0x00010000
#define ASUS_WMI_MGMT_GUID             "97845ED0-4E6D-11DE-8A39-0800200C9A66"

#define kDeliverNotifications "RM,deliverNotifications"

#define AsusSMCEventCode 0x8102

class AsusSMC : public IOService {
    OSDeclareDefaultStructors(AsusSMC)

    VirtualSMCAPI::Plugin vsmcPlugin {
        xStringify(PRODUCT_NAME),
        parseModuleVersion(xStringify(MODULE_VERSION)),
        VirtualSMCAPI::Version,
    };

public:
    bool init(OSDictionary *dictionary) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    IOReturn message(uint32_t type, IOService *provider, void *argument) override;
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties,
                          IOUserClient **handler) override;
    IOReturn controlGetState(uint32_t state[AsusControl::stateCount]);
    IOReturn controlSet(uint32_t what, uint32_t value);
    IOReturn controlKeyActions(uint32_t after, uint32_t *words, size_t *bytes);

    void letSleep();
    void toggleAirplaneMode();
    void toggleTouchpad();
    void toggleALS(bool state);
    void toggleBatteryConservativeMode(bool state);
    void displayOff();

private:
    struct guid_block {
        char guid[16];
        union {
            char object_id[2];
            struct {
                uint8_t notify_id;
                uint8_t reserved;
            };
        };
        uint8_t instance_count;
        uint8_t flags;
    };

    struct wmi_args {
        uint32_t arg0;
        uint32_t arg1;
    } __packed;

    enum {
        kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100), // set disable/enable touchpad (data is bool*)
        kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101), // get disable/enable touchpad (data is bool*)
        kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),   // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
    };

    enum {
        kDaemonKeyboardBacklight = 1,
        kDaemonAirplaneMode = 2,
        kDaemonSleep = 3,
        kDaemonTouchpad = 4,
    };

    static constexpr uint32_t SensorUpdateTimeoutMS {1000};

    static constexpr uint8_t NOTIFY_BRIGHTNESS_UP_MIN = 0x10;
    static constexpr uint8_t NOTIFY_BRIGHTNESS_UP_MAX = 0x1F;

    static constexpr uint8_t NOTIFY_BRIGHTNESS_DOWN_MIN = 0x20;
    static constexpr uint8_t NOTIFY_BRIGHTNESS_DOWN_MAX = 0x2F;

    char wmi_method[5] {0};
    bool wmiGuidValid {false};
    bool atkInitialized {false};
    int wmi_parse_guid(const char *in, char *out);
    int wmi_evaluate_method(uint32_t method_id, uint32_t arg0, uint32_t arg1);
    int wmi_get_devstate(uint32_t dev_id);
    bool wmi_dev_is_present(uint32_t dev_id);
    void parse_WDG();

    void initATKDevice();
    void initALSDevice();
    void initEC0Device();
    void initBattery();
    void initVirtualKeyboard();

    void startATKDevice();
    
    bool refreshALS(bool post);
    bool refreshFan();

    void handleMessage(int code);

    IOACPIPlatformDevice *atkDevice {nullptr};
    IOACPIPlatformDevice *alsDevice {nullptr};
    IOACPIPlatformDevice *ec0Device {nullptr};
    VirtualAppleKeyboard *kbdDevice {nullptr};

    ALSForceBits forceBits;
    _Atomic(uint32_t) currentLux = ATOMIC_VAR_INIT(0);
    _Atomic(uint16_t) currentFanSpeed = ATOMIC_VAR_INIT(0);

    IONotifier *vsmcNotifier {nullptr};

    IOWorkLoop *workloop {nullptr};
    IOCommandGate *command_gate {nullptr};
    IOTimerEventSource *poller {nullptr};

    KernEventServer kev;

    consumer_input csmrreport;
    apple_vendor_top_case_input tcreport;

    bool directACPImessaging {false};
    bool hasKeyboardBacklight {false};
    bool isALSEnabled {true};
    bool isTouchpadEnabled {true};
    bool isPanelBackLightOn {true};
    bool isTACHAvailable {false};
    bool isBatteryRSOCAvailable {false};

    // X515EP controls are one owner for WMI events and battery thresholds.
    // No manual fan curves, power limits, lighting protocol or MSR writes exist in this extension.
    bool x515epControls {false};
    IOLock *controlLock {nullptr};
    IOACPIPlatformDevice *controlACDevice {nullptr};
    IONotifier *controlACNotify {nullptr};
    static IOReturn controlPowerNotification(void *target, void *, UInt32 type, IOService *, void *argument, vm_size_t);
    AsusControl::ProfilePolicy profilePolicy;
    uint32_t controlCaps {0}, lastControlError {0};
    uint32_t controlLastKey {0}, controlKeySequence {0};
    int chargeLimitRequest {-1}, fnlockLastRequest {-1};
    uint32_t hotkeysFirmwareValue {1}, capturedKeys {AsusControl::nativeKeyMask}, controlGpuFan {AsusControl::unknown};
    uint32_t keyBindings[12] {1, 2, 3, 4, 5, 6, 0, 7, 8, 9, 10, 11};
    AsusControl::KeyActionQueue keyActions;
    void dispatchControlAction(unsigned key, uint32_t action);
    void initControls();
    void pollControls();
    void observeControlPowerLocked();
    void reconcileProfileLocked();
    IOReturn setFnlockLocked(int value);
    bool handleControlKey(int code);
    static void sensorTimer(OSObject *owner, IOTimerEventSource *sender);

    uint32_t panelBrightnessLevel {16};
    char backlightEntry[1000];

    int checkBacklightEntry();
    int findBacklightEntry();
    void readPanelBrightnessValue();

    IOReturn postKeyboardInputReport(const void *report, uint32_t reportSize);
    void dispatchCSMRReport(int code, int loop = 1);
    void dispatchTCReport(int code, int loop = 1);

    IONotifier *_publishNotify {nullptr};
    IONotifier *_terminateNotify {nullptr};
    OSSet *_notificationServices {nullptr};
    void registerNotifications(void);
    void notificationHandlerGated(IOService *newService, IONotifier *notifier);
    bool notificationHandler(void *refCon, IOService *newService, IONotifier *notifier);
    void dispatchMessageGated(int *message, void *data);
    void dispatchMessage(int message, void *data);

    void registerVSMC(void);
    static bool vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier);
};

#endif //_AsusSMC_hpp

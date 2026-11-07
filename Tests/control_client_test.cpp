// SPDX-License-Identifier: MIT
// Compile the REAL client adapter against a minimal host IOKit ABI shim.
// This catches 32-bit wire-length / 64-bit size_t mismatches without loading a kext.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../AsusSMC/AsusControlABI.hpp"

using UInt32 = uint32_t;
using IOReturn = uint32_t;
using task_t = void *;
constexpr IOReturn kIOReturnSuccess = 0, kIOReturnNotAttached = 1, kIOReturnNotPrivileged = 2,
    kIOReturnBadArgument = 3, kIOReturnUnsupported = 4, kIOReturnNoMemory = 5,
    kIOReturnError = 6, kIOReturnNoSpace = 7, kIOReturnIOError = 8;
constexpr int kIOClientPrivilegeAdministrator = 1, kIOClientPrivilegeLocalUser = 2;
bool hostPrivilegeAllowed = true;
struct OSDictionary {};
struct OSObject { virtual ~OSObject() = default; };
struct IOExternalMethodDispatch {};
struct IOExternalMethodArguments {
    uint32_t scalarInputCount = 0;
    uint64_t *scalarInput = nullptr;
    uint32_t structureInputSize = 0;
    void *structureOutput = nullptr;
    uint32_t structureOutputSize = 0; // Deliberately the actual IOKit wire type, NOT size_t.
    uint32_t adjacentGuard = 0x12345678;
};
class IOService : public OSObject {
public:
    virtual bool start(IOService *) { return true; }
};
class IOUserClient : public IOService {
public:
    virtual bool initWithTask(task_t, void *, UInt32, OSDictionary *) { return true; }
    virtual IOReturn clientClose() { return kIOReturnSuccess; }
    virtual IOReturn externalMethod(uint32_t, IOExternalMethodArguments *, IOExternalMethodDispatch *, OSObject *, void *) { return kIOReturnUnsupported; }
    static IOReturn clientHasPrivilege(task_t, int) { return hostPrivilegeAllowed ? kIOReturnSuccess : kIOReturnNotPrivileged; }
    bool attach(IOService *) { return true; }
    void detach(IOService *) {}
    void terminate() {}
    void release() { delete this; }
};
#define OSDeclareDefaultStructors(type)
#define OSDefineMetaClassAndStructors(type, base)
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)

class AsusSMC : public IOService {
public:
    bool x515epControls = true;
    int lockStorage = 0;
    void *controlLock = &lockStorage;
    size_t returnedBytes = 8, lastCapacity = 0;
    IOReturn helperResult = kIOReturnSuccess;
    uint32_t lastAfter = 0, writes = 0;
    IOReturn controlGetState(uint32_t *state) {
        std::memset(state, 0, AsusControl::stateCount * sizeof(uint32_t));
        state[0] = AsusControl::abiVersion;
        return kIOReturnSuccess;
    }
    IOReturn controlSet(uint32_t, uint32_t) { ++writes; return kIOReturnSuccess; }
    IOReturn controlKeyActions(uint32_t after, uint32_t *words, size_t *bytes) {
        if (!words || !bytes || *bytes < (2 + 64 * 4) * sizeof(uint32_t)) return kIOReturnBadArgument;
        lastAfter = after; lastCapacity = *bytes;
        words[0] = 42; words[1] = 0;
        *bytes = returnedBytes; // Can intentionally over-report to exercise the adapter's bounds guard.
        return helperResult;
    }
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **);
};
#include "../AsusSMC/AsusControlClient.hpp"

int main() {
    static_assert(sizeof(IOExternalMethodArguments::structureOutputSize) == 4, "wire length must remain uint32_t");
    static_assert(sizeof(size_t) == 8, "run this regression on a 64-bit host");
    AsusSMC driver;
    AsusSMCControlClient client;
    assert(client.initWithTask(nullptr, nullptr, 0, nullptr));
    assert(client.start(&driver));
    uint32_t output[2 + 64 * 4] {};
    uint64_t scalars[2] {17, 0};
    IOExternalMethodArguments args;
    args.scalarInputCount = 1; args.scalarInput = scalars;
    args.structureOutput = output; args.structureOutputSize = sizeof(output);
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnSuccess);
    assert(driver.lastCapacity == sizeof(output) && driver.lastAfter == 17);
    assert(args.structureOutputSize == 8 && args.adjacentGuard == 0x12345678);

    args.structureOutputSize = sizeof(output);
    driver.returnedBytes = sizeof(output) + 1;
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnNoSpace);
    assert(args.structureOutputSize == sizeof(output) && args.adjacentGuard == 0x12345678);
    driver.returnedBytes = static_cast<size_t>(UINT32_MAX) + 1;
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnNoSpace);
    assert(args.adjacentGuard == 0x12345678);

    driver.returnedBytes = 8; driver.helperResult = kIOReturnIOError;
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnIOError);
    assert(args.structureOutputSize == sizeof(output));
    driver.helperResult = kIOReturnSuccess;
    scalars[0] = static_cast<uint64_t>(UINT32_MAX) + 1;
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    scalars[0] = 17; args.structureOutput = nullptr;
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    args.structureOutput = output; args.structureOutputSize = 4;
    assert(client.externalMethod(AsusControl::getKeyActions, &args, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

    args.scalarInputCount = 0; args.structureOutputSize = sizeof(output);
    assert(client.externalMethod(AsusControl::getState, &args, nullptr, nullptr, nullptr) == kIOReturnSuccess);
    assert(output[0] == AsusControl::abiVersion && args.structureOutputSize == AsusControl::stateCount * sizeof(uint32_t));
    assert(client.externalMethod(99, &args, nullptr, nullptr, nullptr) == kIOReturnUnsupported);
    hostPrivilegeAllowed = false;
    AsusSMCControlClient denied;
    assert(denied.initWithTask(nullptr, nullptr, 0, nullptr) && denied.start(&driver));
    assert(denied.externalMethod(AsusControl::getState, &args, nullptr, nullptr, nullptr) == kIOReturnNotPrivileged);
    assert(driver.writes == 0);
}

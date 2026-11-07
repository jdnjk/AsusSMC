// SPDX-License-Identifier: MIT
// Included by AsusSMC.cpp so the existing Xcode sources list needs no extra translation unit.
#ifndef ASUS_CONTROL_CLIENT_HPP
#define ASUS_CONTROL_CLIENT_HPP
class AsusSMCControlClient : public IOUserClient {
    OSDeclareDefaultStructors(AsusSMCControlClient)
    AsusSMC *driver {nullptr};
    bool allowed {false};
public:
    bool initWithTask(task_t task, void *token, UInt32 type, OSDictionary *properties) override {
        if (!IOUserClient::initWithTask(task, token, type, properties)) return false;
        allowed = clientHasPrivilege(task, kIOClientPrivilegeAdministrator) == kIOReturnSuccess ||
                  clientHasPrivilege(task, kIOClientPrivilegeLocalUser) == kIOReturnSuccess;
        return true;
    }
    bool start(IOService *provider) override {
        driver = OSDynamicCast(AsusSMC, provider);
        return driver && IOUserClient::start(provider);
    }
    IOReturn clientClose() override { terminate(); return kIOReturnSuccess; }
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override {
        if (!driver || !args) return kIOReturnNotAttached;
        if (!allowed) return kIOReturnNotPrivileged;
        if (selector == AsusControl::getState) {
            const size_t bytes = AsusControl::stateCount * sizeof(uint32_t);
            if (!args->structureOutput || args->structureOutputSize < bytes || args->scalarInputCount || args->structureInputSize)
                return kIOReturnBadArgument;
            uint32_t state[AsusControl::stateCount];
            IOReturn r = driver->controlGetState(state);
            if (r == kIOReturnSuccess) { memcpy(args->structureOutput, state, bytes); args->structureOutputSize = bytes; }
            return r;
        }
        if (selector == AsusControl::getKeyActions) {
            if (args->scalarInputCount != 1 || args->scalarInput[0] > UINT32_MAX || args->structureInputSize)
                return kIOReturnBadArgument;
            return driver->controlKeyActions((uint32_t)args->scalarInput[0], (uint32_t *)args->structureOutput, &args->structureOutputSize);
        }
        if (selector == AsusControl::set) {
            if (args->scalarInputCount != 2 || args->structureInputSize || args->scalarInput[0] > UINT32_MAX ||
                args->scalarInput[1] > UINT32_MAX) return kIOReturnBadArgument;
            return driver->controlSet((uint32_t)args->scalarInput[0], (uint32_t)args->scalarInput[1]);
        }
        // No raw DEVS, manual curves, power limits, lighting or EC-register access API.
        return kIOReturnUnsupported;
    }
};
OSDefineMetaClassAndStructors(AsusSMCControlClient, IOUserClient)

IOReturn AsusSMC::newUserClient(task_t task, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler) {
    if (!handler || !x515epControls || !controlLock) return kIOReturnUnsupported;
    *handler = nullptr;
    if (type != 0) return kIOReturnBadArgument;
    auto client = new AsusSMCControlClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, securityID, type, properties)) { client->release(); return kIOReturnError; }
    if (!client->attach(this)) { client->release(); return kIOReturnError; }
    if (!client->start(this)) { client->detach(this); client->release(); return kIOReturnError; }
    *handler = client;
    return kIOReturnSuccess;
}
#endif

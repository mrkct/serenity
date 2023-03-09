#pragma once

#include <AK/OwnPtr.h>
#include <AK/Types.h>
#include <Kernel/Library/LockRefPtr.h>
#include <Kernel/Storage/SD/SDDevice.h>
#include <Kernel/Storage/StorageController.h>
#include <Kernel/Storage/StorageDevice.h>

namespace Kernel {

class AsyncBlockDeviceRequest;

class SDHostController final : public StorageController {
public:
    static ErrorOr<NonnullLockRefPtr<SDHostController>> try_initialize();
    virtual ~SDHostController() override = default;

    virtual LockRefPtr<StorageDevice> device(u32 index) const override;
    virtual bool reset() override;
    virtual bool shutdown() override;
    virtual size_t devices_count() const override { return m_devices.size(); }
    virtual void complete_current_request(AsyncDeviceRequest::RequestResult) override;

private:
    SDHostController();

    ErrorOr<void> try_initialize_all_devices();

    Vector<NonnullLockRefPtr<SDDevice>> m_devices;
};
}

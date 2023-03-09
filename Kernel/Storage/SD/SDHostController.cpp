#include <Kernel/Arch/aarch64/RPi/SD.h>
#include <Kernel/Library/LockRefPtr.h>
#include <Kernel/Storage/SD/SDHostController.h>

namespace Kernel {

ErrorOr<NonnullLockRefPtr<SDHostController>> SDHostController::try_initialize()
{
    auto sdhc = TRY(adopt_nonnull_lock_ref_or_enomem(new SDHostController()));
    TRY(sdhc->try_initialize_all_devices());

    return sdhc;
}

ErrorOr<void> SDHostController::try_initialize_all_devices()
{
    for (auto& device : m_devices) {
        TRY(device->try_initialize());
    }

    return {};
}

bool SDHostController::reset()
{
    TODO();
}

bool SDHostController::shutdown()
{
    TODO();
}

LockRefPtr<StorageDevice> SDHostController::device(u32 index) const
{
    if (index >= m_devices.size())
        return nullptr;
    return m_devices[index];
}

void SDHostController::complete_current_request(AsyncDeviceRequest::RequestResult)
{
    VERIFY_NOT_REACHED();
}

SDHostController::SDHostController()
    : StorageController(0) // FIXME: Need to check this again
{
#if ARCH(AARCH64)
    if (auto rpi_sd = adopt_nonnull_lock_ref_or_enomem(new RPi::RPiSDCard(
            StorageDevice::LUNAddress { 0, 0, 0 },
            23));
        !rpi_sd.is_error())
        m_devices.append(rpi_sd.release_value());
#endif
}

}

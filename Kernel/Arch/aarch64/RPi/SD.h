#pragma once

#include <Kernel/Storage/SD/SDDevice.h>

namespace Kernel::RPi {

class RPiSDCard : public SDDevice {
public:
    RPiSDCard(StorageDevice::LUNAddress, u32 hardware_relative_controller_id);
    virtual ~RPiSDCard() override = default;

protected:
    virtual ErrorOr<u32> retrieve_sd_clock_frequency() override;
    virtual SDRegisters volatile* get_register_map_base_address() override { return m_registers; }

    SDDevice::SDRegisters volatile* m_registers;
};

}

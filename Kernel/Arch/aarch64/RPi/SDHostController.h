#pragma once

#include <Kernel/Storage/SD/Registers.h>
#include <Kernel/Storage/SD/SDHostController.h>

namespace Kernel::RPi {

class SDHostController : public ::SDHostController {
public:
    SDHostController(u32 hardware_relative_controller_id);
    virtual ~SDHostController() override = default;

protected:
    virtual SD::SDRegisters volatile* get_register_map_base_address() override { return m_registers; }

    SD::SDRegisters volatile* m_registers;
};

}

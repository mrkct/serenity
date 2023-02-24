#pragma once

#include <AK/Types.h> 

namespace Kernel::RPi {

class SD {
public:
    static SD& the();

    bool is_cart_inserted() {
        constexpr u32 CARD_INSERTED = 1 << 16;
        return m_registers->present_state & CARD_INSERTED;
    }

    void testing();
private:
    // SD Host Controller Simplified Specification Version 3.00
    // NOTE: The registers must be 32 bits, because of a quirk in the RPI.
    struct SDRegisters {
        u32 argument_2;
        u32 block_size_and_block_count;
        u32 argument_1;
        u32 transfer_mode_and_command;
        u32 response_0;
        u32 response_1;
        u32 response_2;
        u32 response_3;
        u32 buffer_data_port;
        u32 present_state;
        u32 host_configuration_0;
        u32 host_configuration_1;
        u32 interrupt_status;
        u32 interrupt_status_enable;
        u32 interrupt_signal_enable;
        u32 host_configuration_2;
        u32 capabilities_0;
        u32 capabilities_1;
        u32 maximum_current_capabilities;
        u32 maximum_current_capabilities_reserved;
        u32 force_event_for_auto_cmd_error_status;
        u32 adma_error_status;
        u32 adma_system_address[2];
        u32 preset_value[4];
        u32 reserved_0[28];
        u32 shared_bus_control;
        u32 reserved_1[6];
        u32 slot_interrupt_status_and_version;
    } __attribute__((packed));

    SD();

    enum class SDHostVersion {
        Version1 = 0x0,
        Version2 = 0x1,
        Version3 = 0x2,
        Unknown
    };

    SDHostVersion host_version() {
        const u16 host_controller_version_register = m_registers->slot_interrupt_status_and_version >> 16;
        switch (host_controller_version_register & 0xff) {
            case 0x0: return SDHostVersion::Version1;
            case 0x1: return SDHostVersion::Version2;
            case 0x2: return SDHostVersion::Version3;
            default:  return SDHostVersion::Unknown;
        }
    }
    bool reset_host_controller();
    void enable_interrupts_on_card_insertion_and_removal();

    volatile struct SDRegisters *m_registers;

    bool m_card_is_inserted { false };
};

}

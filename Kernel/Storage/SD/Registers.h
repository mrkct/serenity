#pragma once

#include <AK/Types.h>

namespace Kernel::SD {

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

struct OperatingConditionRegister {
    u32 : 15;
    u32 vdd_voltage_window_27_28 : 1;
    u32 vdd_voltage_window_28_29 : 1;
    u32 vdd_voltage_window_29_30 : 1;
    u32 vdd_voltage_window_30_31 : 1;
    u32 vdd_voltage_window_31_32 : 1;
    u32 vdd_voltage_window_32_33 : 1;
    u32 vdd_voltage_window_33_34 : 1;
    u32 vdd_voltage_window_34_35 : 1;
    u32 vdd_voltage_window_35_36 : 1;
    u32 switching_to_18v_accepted : 1;
    u32 : 2;
    u32 over_2tb_support_status : 1;
    u32 : 1;
    u32 uhs2_card_status : 1;
    u32 card_capacity_status : 1;
    u32 card_power_up_status : 1;

    static OperatingConditionRegister from_acmd41_response(u32 value)
    {
        union {
            u32 x;
            struct OperatingConditionRegister ocr;
        } u;
        u.x = value;
        return u.ocr;
    }
} __attribute__((packed));
static_assert(sizeof(OperatingConditionRegister) == sizeof(u32));

struct CardIdentificationRegister {
    u32 : 1;
    u32 crc7_checksum : 7;
    u32 manufacturing_date : 12;
    u32 : 4;
    u32 product_serial_number : 32;
    u32 product_revision : 8;
    u64 product_name : 40;
    u32 oem_id : 16;
    u32 manufacturer_id : 8;

    static struct CardIdentificationRegister from_cid_response(u32 response[4])
    {
        union {
            u32 x[4];
            struct CardIdentificationRegister cid;
        } u;
        u.x[0] = response[0];
        u.x[1] = response[1];
        u.x[2] = response[2];
        u.x[3] = response[3];
        return u.cid;
    }
} __attribute__((packed));
static_assert(sizeof(CardIdentificationRegister) == sizeof(u32) * 4);

struct SDConfigurationRegister {
    u32 scr_structure : 4;
    u32 sd_specification : 4;
    u32 data_status_after_erase : 1;
    u32 sd_security : 3;
    u32 sd_bus_widths : 4;
    u32 sd_specification3 : 1;
    u32 extended_security : 4;
    u32 sd_specification4 : 1;
    u32 sd_specification_x : 4;
    u32 : 1;
    u32 command_support : 5;
    u32 : 32;

    static struct SDConfigurationRegister from_u64(u64 x)
    {
        union {
            u64 x;
            struct SDConfigurationRegister scr;
        } u;
        u.x = x;
        return u.scr;
    }
} __attribute__((packed));
static_assert(sizeof(SDConfigurationRegister) == sizeof(u64));

}

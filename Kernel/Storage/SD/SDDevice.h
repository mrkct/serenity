#pragma once

#include <AK/Function.h>
#include <AK/Result.h>
#include <AK/Types.h>
#include <Kernel/Locking/Mutex.h>
#include <Kernel/Storage/StorageDevice.h>

namespace Kernel {

class SDDevice : public StorageDevice {
  public:
    ErrorOr<void> try_initialize();

    // ^StorageDevice
    virtual bool can_read(OpenFileDescription const &fd,
                          u64 offset) const override;
    virtual bool can_write(OpenFileDescription const &fd,
                           u64 offset) const override;
    virtual CommandSet command_set() const override { return CommandSet::SDIO; }

    // ^BlockDevice
    virtual void start_request(AsyncBlockDeviceRequest &) override;

  protected:
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

    virtual ErrorOr<u32> retrieve_sd_clock_frequency() = 0;
    virtual SDRegisters volatile *get_register_map_base_address() = 0;

    SDDevice(StorageDevice::LUNAddress, u32 hardware_relative_controller_id);

  private:
    bool is_cart_inserted() const {
        constexpr u32 CARD_INSERTED = 1 << 16;
        return m_registers->present_state & CARD_INSERTED;
    }

    enum class CommandIndex {
        GoIdleState = 0,
        AllSendCid = 2,
        SendRelativeAddr = 3,
        AppSetBusWidth = 6,
        SelectCard = 7,
        SendIfCond = 8,
        ReadSingleBlock = 17,
        ReadMultipleBlock = 18,
        WriteSingleBlock = 24,
        WriteMultipleBlock = 25,
        AppSendOpCond = 41,
        AppSendCsr = 51,
        AppCmd = 55,
    };

    enum class CommandType { Normal, Suspend, Resume, Abort };

    struct EmmcCommand {
        u8 resp_a : 1;
        u8 block_count : 1;
        u8 auto_command : 2;
        u8 direction : 1;
        u8 multiblock : 1;
        u16 resp_b : 10;
        u8 response_type : 2;
        u8 res0 : 1;
        u8 crc_enable : 1;
        u8 idx_enable : 1;
        u8 is_data : 1;
        u8 type : 2;
        u8 index : 6;
        u8 res1 : 2;

        static u32 to_u32(EmmcCommand cmd) {
            union {
                u32 x;
                struct EmmcCommand cmd;
            } u;
            u.cmd = cmd;
            return u.x;
        }

        static EmmcCommand from_u32(u32 value) {
            union {
                u32 x;
                struct EmmcCommand cmd;
            } u;
            u.x = value;
            return u.cmd;
        }
    } __attribute__((packed));
    static_assert(sizeof(EmmcCommand) == sizeof(u32));

    EmmcCommand const &get_command(CommandIndex code) const;
    static constexpr EmmcCommand build_cmd0();
    static constexpr EmmcCommand build_cmd2();
    static constexpr EmmcCommand build_cmd3();
    static constexpr EmmcCommand build_cmd6();
    static constexpr EmmcCommand build_cmd7();
    static constexpr EmmcCommand build_cmd8();
    static constexpr EmmcCommand build_cmd17();
    static constexpr EmmcCommand build_cmd18();
    static constexpr EmmcCommand build_cmd24();
    static constexpr EmmcCommand build_cmd25();
    static constexpr EmmcCommand build_cmd41();
    static constexpr EmmcCommand build_cmd51();
    static constexpr EmmcCommand build_cmd55();

    enum class ResponseType {
        NoResponse,
        ResponseOf136Bits,
        ResponseOf48Bits,
        ResponseOf48BitsWithBusy
    };

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

        static OperatingConditionRegister from_acmd41_response(u32 value) {
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

        static struct CardIdentificationRegister
        from_cid_response(u32 response[4]) {
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

        static struct SDConfigurationRegister from_u64(u64 x) {
            union {
                u64 x;
                struct SDConfigurationRegister scr;
            } u;
            u.x = x;
            return u.scr;
        }
    } __attribute__((packed));
    static_assert(sizeof(SDConfigurationRegister) == sizeof(u64));

    enum class SDHostVersion {
        Version1 = 0x0,
        Version2 = 0x1,
        Version3 = 0x2,
        Unknown
    };

    SDHostVersion host_version() {
        const u16 host_controller_version_register =
            m_registers->slot_interrupt_status_and_version >> 16;
        switch (host_controller_version_register & 0xff) {
        case 0x0:
            return SDHostVersion::Version1;
        case 0x1:
            return SDHostVersion::Version2;
        case 0x2:
            return SDHostVersion::Version3;
        default:
            return SDHostVersion::Unknown;
        }
    }
    enum class CardAddressingMode { ByteAddressing, BlockAddressing };
    CardAddressingMode card_addressing_mode() const {
        return m_ocr.card_capacity_status ? CardAddressingMode::BlockAddressing
                                          : CardAddressingMode::ByteAddressing;
    }

    ErrorOr<void> reset_host_controller();

    u32 last_sent_command() { return m_registers->transfer_mode_and_command; }
    bool currently_active_command_uses_transfer_complete_interrupt();

    bool command_uses_transfer_complete_interrupt(u32 cmd) const;
    bool command_requires_dat_line(EmmcCommand) const;
    bool command_is_abort(EmmcCommand) const;
    ResponseType response_type(EmmcCommand) const;

    ErrorOr<void> sd_clock_supply(u64 frequency);
    void sd_clock_stop();
    ErrorOr<void> sd_clock_frequency_change(u64 frequency);

    struct Response {
        u32 response[4];
    };
    ErrorOr<void> issue_command(CommandIndex, u32 argument);
    ErrorOr<Response> wait_for_response();

    bool retry_with_timeout(Function<bool()>, i64 delay_between_tries = 0);

    // FIXME: Probably better to return how many bytes were actually read.
    ErrorOr<void> sync_data_read_command(CommandIndex command_index,
                                         u32 argument, u32 block_count,
                                         u32 block_size, u8 *out);

    volatile struct SDRegisters *m_registers;

    bool m_card_is_inserted{false};
    u32 m_last_sent_command{0};
    struct OperatingConditionRegister m_ocr {};
    struct CardIdentificationRegister m_cid {};
    struct SDConfigurationRegister m_scr {};
    u32 m_rca{0};
    Mutex m_lock{"SDDevice"sv};
};

} // namespace Kernel

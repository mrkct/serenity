#pragma once

#include <AK/Function.h>
#include <AK/Result.h>
#include <AK/Types.h>
#include <Kernel/Locking/Mutex.h>
#include <Kernel/Storage/SD/Commands.h>
#include <Kernel/Storage/SD/Registers.h>
#include <Kernel/Storage/SD/SDMemoryCard.h>

namespace Kernel {

class SDHostController : public StorageController {
    friend class SDMemoryCard;

public:
    static ErrorOr<NonnullLockRefPtr<SDHostController>> try_initialize();

    virtual ~SDHostController() = default;

    virtual LockRefPtr<StorageDevice> device(u32 index) const override { return index == 0 ? m_card : nullptr; }
    virtual bool reset() override;
    virtual bool shutdown() override;
    virtual size_t devices_count() const override { return m_card ? 1 : 0; }
    virtual void complete_current_request(AsyncDeviceRequest::RequestResult) override;

protected:
    virtual SD::SDRegisters volatile* get_register_map_base_address() = 0;

    SDHostController(u32 hardware_relative_controller_id);

private:
    ErrorOr<void> initialize();
    ErrorOr<NonnullLockRefPtr<SDMemoryCard>> try_initialize_inserted_card();

    bool is_card_inserted() const
    {
        constexpr u32 CARD_INSERTED = 1 << 16;
        return m_registers->present_state & CARD_INSERTED;
    }

    enum class SDHostVersion {
        Version1 = 0x0,
        Version2 = 0x1,
        Version3 = 0x2,
        Unknown
    };

    SDHostVersion host_version()
    {
        const u16 host_controller_version_register = m_registers->slot_interrupt_status_and_version >> 16;
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

    ErrorOr<void> reset_host_controller();

    SD::Command last_sent_command() { return SD::Command::from_u32(m_registers->transfer_mode_and_command); }
    bool currently_active_command_uses_transfer_complete_interrupt();

    ErrorOr<void> sd_clock_supply(u64 frequency);
    void sd_clock_stop();
    ErrorOr<void> sd_clock_frequency_change(u64 frequency);
    ErrorOr<u32> retrieve_sd_clock_frequency();

    struct Response {
        u32 response[4];
    };
    ErrorOr<void> issue_command(SD::CommandIndex, u32 argument);
    ErrorOr<Response> wait_for_response();

    bool retry_with_timeout(Function<bool()>, i64 delay_between_tries = 0);

    // FIXME: Probably better to return how many bytes were actually read.
    ErrorOr<void> sync_data_read_command(SD::CommandIndex, u32 argument, u32 block_count, u32 block_size, u8* out);

    volatile struct SD::SDRegisters* m_registers;
    LockRefPtr<SDMemoryCard> m_card { nullptr };

    u32 m_hardware_relative_controller_id { 0 };
    Mutex m_lock { "SDHostController"sv };
};

}

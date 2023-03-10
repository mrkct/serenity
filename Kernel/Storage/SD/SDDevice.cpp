#include <AK/Format.h>
#include <Kernel/Arch/aarch64/RPi/GPIO.h>
#include <Kernel/Arch/aarch64/RPi/MMIO.h>
#include <Kernel/Arch/aarch64/RPi/SD.h>
#include <Kernel/Time/TimeManagement.h>

namespace Kernel {

const i64 ONE_SECOND = 1'000'000'000;
const i64 TENTH_OF_A_SECOND = ONE_SECOND / 10;

// References:
// - BCM2835: BCM2835 ARM Peripherals: Addresses of the SD Host Controller
// registers
// - SDHCI: SDHCI Simplified Host Controller Specification Version 3.0
// - PLSS: Physical Layer Simplified Specification Version 9.00

static void delay(i64 nanoseconds) {
    auto start = TimeManagement::the().monotonic_time().to_nanoseconds();
    auto end = start + nanoseconds;
    while (TimeManagement::the().monotonic_time().to_nanoseconds() < end)
        ;
}

// In "m_registers->host_configuration_1"
// In sub-register "Clock Control"
constexpr u32 INTERNAL_CLOCK_ENABLE = 1 << 0;
constexpr u32 INTERNAL_CLOCK_STABLE = 1 << 1;
constexpr u32 SD_CLOCK_ENABLE = 1 << 2;

// In sub-register "Software Reset"
constexpr u32 SOFTWARE_RESET_FOR_ALL = 0x01000000;

// In Interrupt Status Register
const u32 COMMAND_COMPLETE = 1 << 0;
const u32 TRANSFER_COMPLETE = 1 << 1;
const u32 BUFFER_READ_READY = 1 << 5;

// Commands, defined in PLSS 4.7.4 with the format described in BCM2835 "CMDTM
// Register"

/*
constexpr u32 GO_IDLE_STATE = 0x00000000;
constexpr u32 SEND_IF_COND = 0x08020000;
constexpr u32 CMD_ALL_SEND_CID = 0x02010000;
constexpr u32 CMD_SEND_REL_ADDR = 0x03020000;
constexpr u32 APP_CMD = 0x37000000;
constexpr u32 APP_SEND_OP_COND = 0x29020000;
constexpr u32 CMD_READ_SINGLE_BLOCK = 0x11220010;
constexpr u32 CMD_SELECT_CARD = 0x07030000;
constexpr u32 CMD_SET_BUS_WIDTH = 0x06020000;
constexpr u32 APP_SEND_SCR = 0x33220010;
*/

// PLSS 5.1: all voltage windows
constexpr u32 ACMD41_VOLTAGE = 0x00ff8000;
// check if CCS bit is set => SDHC support
constexpr u32 ACMD41_SDHC = 0x40000000;
// PLSS 4.2.3.1: All voltage windows, XPC = 1, SDHC = 1
constexpr u32 ACMD41_ARG = 0x50ff8000;

SDDevice::SDDevice(StorageDevice::LUNAddress lun_address,
                   u32 hardware_relative_controller_id)
    : StorageDevice(lun_address, hardware_relative_controller_id, 512,
                    (u64)2 * 1024 * 1024 * 1024 / 512) {}

void SDDevice::start_request(AsyncBlockDeviceRequest &request) {
    MutexLocker locker(m_lock);

    if (!is_cart_inserted()) {
        request.complete(AsyncDeviceRequest::Failure);
        return;
    }

    if (request.request_type() == AsyncBlockDeviceRequest::RequestType::Write) {
        TODO();
    }

    VERIFY(request.block_size() <= 512);
    for (u32 block = 0; block < request.block_count(); ++block) {
        u8 data[1024]; // FIXME: Horrible

        auto r = sync_data_read_command(CommandIndex::ReadSingleBlock,
                                        512 * (request.block_index() + block),
                                        1, 512, data);

        if (r.is_error()) {
            request.complete(AsyncDeviceRequest::Failure);
            return;
        }

        MUST(request.buffer().write(data, block * request.block_size(),
                                    request.block_size()));
    }
    request.complete(AsyncDeviceRequest::Success);
}

bool SDDevice::can_read(OpenFileDescription const &fd, u64 offset) const {
    if (!is_cart_inserted())
        return false;

    return StorageDevice::can_read(fd, offset);
}

bool SDDevice::can_write(OpenFileDescription const &fd, u64 offset) const {
    if (!is_cart_inserted())
        return false;

    // FIXME: Check if the card is write-protected.

    return StorageDevice::can_write(fd, offset);
}

bool SDDevice::command_uses_transfer_complete_interrupt(u32) const {
    // FIXME: I don't know how to determine this.
    //      probably sth about: TM_AUTO_CMD_EN?
    return false;
}

bool SDDevice::command_requires_dat_line(EmmcCommand command) const {
    // BCM2835 "CMDTM Register"
    return command.is_data;
}

bool SDDevice::command_is_abort(EmmcCommand command) const {
    // BCM2835 "CMDTM Register"
    return command.type == static_cast<u8>(SDDevice::CommandType::Abort);
}

SDDevice::ResponseType SDDevice::response_type(EmmcCommand command) const {
    switch (command.response_type) {
    case 0b00:
        return ResponseType::NoResponse;
    case 0b01:
        return ResponseType::ResponseOf136Bits;
    case 0b10:
        return ResponseType::ResponseOf48Bits;
    case 0b11:
        return ResponseType::ResponseOf48BitsWithBusy;
    }
    VERIFY_NOT_REACHED();
}

ErrorOr<void> SDDevice::try_initialize() {
    m_registers = get_register_map_base_address();
    if (!m_registers)
        return EIO;

    if (host_version() != SDHostVersion::Version3)
        return ENOTSUP;

    TRY(reset_host_controller());

    // FIXME: Makes sense, but I couldn't find it in the spec.
    m_registers->interrupt_status_enable = 0xffffffff;
    m_registers->interrupt_signal_enable = 0xffffffff;

    // PLSS: 4.2 Card Identification Mode
    // After power-on ...the cards are initialized with ... 400KHz clock
    // frequency.
    TRY(sd_clock_supply(400000));

    // PLSS: 4.2.3 Card Initialization and Identification Process
    // Also see Figure 4-2 in the PLSS spec for a flowchart of the
    // initialization process. Note that the steps correspond to the steps in
    // the flowchart, although I made up the numbering and text

    // 1. Send CMD0 (GO_IDLE_STATE) to the card
    TRY(issue_command(CommandIndex::GoIdleState, 0));
    TRY(wait_for_response());

    // 2. Send CMD8 (SEND_IF_COND) to the card
    // SD interface condition: 7:0 = check pattern, 11:8 = supply voltage
    //      0x1aa: check pattern = 10101010, supply voltage = 1 => 2.7-3.6V
    const u32 VOLTAGE_WINDOW = 0x1aa;
    TRY(issue_command(CommandIndex::SendIfCond, VOLTAGE_WINDOW));
    auto interface_condition_response = wait_for_response();

    // 3. If the card does not respond to CMD8 it means that (Ver2.00 or later
    // SD Memory Card(voltage mismatch) or Ver1.X SD Memory Card or not SD
    // Memory Card)
    if (interface_condition_response.is_error()) {
        // TODO: This is supposed to be the "No Response" branch of the
        // flowchart in Figure 4-2 of the PLSS spec
        return ENOTSUP;
    }

    // 4. If the card responds to CMD8, but it's not a valid response then the
    // card is not usable
    if (interface_condition_response.value().response[0] != VOLTAGE_WINDOW) {
        // FIXME: We should probably try again with a lower voltage window
        return ENODEV;
    }

    // 5. Send ACMD41 (SEND_OP_COND) with HCS=1 to the card, repeat this until
    // the card is ready or timeout
    m_ocr = {};
    bool card_is_usable = true;
    if (!retry_with_timeout(
            [&]() {
                if (issue_command(CommandIndex::AppCmd, 0).is_error() ||
                    wait_for_response().is_error())
                    return false;

                if (issue_command(CommandIndex::AppSendOpCond, ACMD41_ARG)
                        .is_error())
                    return false;

                if (auto acmd41_response = wait_for_response();
                    !acmd41_response.is_error()) {

                    // 20. check if card supports voltage windows we requested
                    // and sdhc
                    u32 response = acmd41_response.value().response[0];
                    if ((response & ACMD41_VOLTAGE) != ACMD41_VOLTAGE) {
                        card_is_usable = false;
                        return false;
                    }

                    m_ocr = OperatingConditionRegister::from_acmd41_response(
                        acmd41_response.value().response[0]);
                }

                return m_ocr.card_power_up_status == 1;
            },
            100)) {
        return card_is_usable ? EIO : ENODEV;
    }

    // 6. If you requested to switch to 1.8V, and the card accepts, execute a
    // voltage switch sequence
    //    (we didn't ask it)

    // 7. Send CMD2 (ALL_SEND_CID) to the card
    TRY(issue_command(CommandIndex::AllSendCid, 0));
    auto all_send_cid_response = TRY(wait_for_response());
    m_cid = CardIdentificationRegister::from_cid_response(
        all_send_cid_response.response);

    // 8. Send CMD3 (SEND_RELATIVE_ADDR) to the card
    TRY(issue_command(CommandIndex::SendRelativeAddr, 0));
    auto send_relative_addr_response = TRY(wait_for_response());
    m_rca = send_relative_addr_response.response[0];

    // Extra steps:
    TRY(issue_command(CommandIndex::SelectCard, m_rca));
    TRY(wait_for_response());

    u32 scr[2];
    TRY(issue_command(CommandIndex::AppCmd, m_rca));
    TRY(wait_for_response());
    TRY(sync_data_read_command(CommandIndex::AppSendCsr, 0, 1, 8, (u8 *)scr));
    m_scr = SDConfigurationRegister::from_u64(static_cast<u64>(scr[1]) << 32 |
                                              scr[0]);

    TRY(issue_command(CommandIndex::AppCmd, m_rca));
    TRY(wait_for_response());
    TRY(issue_command(CommandIndex::AppSetBusWidth,
                      0x2)); // 0b00=1 bit bus, 0b10=4 bit bus
    TRY(wait_for_response());

    dbgln("SD: init done");

    return {};
}

bool SDDevice::retry_with_timeout(Function<bool()> f, i64 delay_between_tries) {
    int timeout = 1000;
    bool success = false;
    while (!success && timeout > 0) {
        success = f();
        if (!success)
            delay(delay_between_tries);
        timeout--;
    }
    return timeout > 0;
}

ErrorOr<void> SDDevice::issue_command(CommandIndex index, u32 argument) {
    // SDHC 3.7.1 Transaction Control without Data Transfer Using DAT Line
    constexpr u32 COMMAND_INHIBIT = 1 << 1;
    auto cmd = get_command(index);

    // 1. Check Command Inhibit (CMD) in the Present State register.
    //    Repeat this step until **Command Inhibit (CMD)** is 0.
    //    That is, when Command Inhibit (CMD) is 1, the Host Driver
    //    shall not issue an SD Command.
    if (!retry_with_timeout(
            [&]() { return !(m_registers->present_state & COMMAND_INHIBIT); },
            100000)) {
        return EIO;
    }

    // 2. If the Host Driver issues an SD Command using DAT lines
    //    including busy signal, go to step (3).
    //    If without using DAT lines including busy signal, go to step (5).
    // 3. If the Host Driver is issuing an abort command, go to step (5). In the
    // case of non-abort
    //    command, go to step (4).
    if (command_requires_dat_line(cmd) && command_is_abort(cmd)) {

        // 4. Check Command Inhibit (DAT) in the Present State register. Repeat
        // this step until
        //    Command Inhibit (DAT) is set to 0.
        constexpr u32 DATA_INHIBIT = 1 << 2;
        if (!retry_with_timeout(
                [&]() { return !(m_registers->present_state & DATA_INHIBIT); },
                100)) {
            return EIO;
        }
    }

    // 5. Set registers as described in Table 1-2 except Command register.
    m_registers->argument_1 = argument;

    // 6. Set the Command register.
    m_registers->transfer_mode_and_command = SDDevice::EmmcCommand::to_u32(cmd);

    // 7. Perform Command Completion Sequence in accordance with 3.7.1.2.
    // Done in wait_for_response()

    return {};
}

ErrorOr<SDDevice::Response> SDDevice::wait_for_response() {
    // SDHC 3.7.1.2 The Sequence to Finalize a Command

    // 1. Wait for the Command Complete Interrupt. If the Command Complete
    // Interrupt has occurred,
    //    go to step (2).
    if (!retry_with_timeout(
            [&]() { return m_registers->interrupt_status & COMMAND_COMPLETE; },
            10000)) {
        return EIO;
    }

    // 2. Write 1 to Command Complete in the Normal Interrupt Status register to
    // clear this bit
    m_registers->interrupt_status = COMMAND_COMPLETE;

    // 3. Read the Response register(s) to get the response.
    struct Response r = {};
    auto cmd = EmmcCommand::from_u32(last_sent_command());
    switch (response_type(cmd)) {
    case ResponseType::NoResponse:
        break;
    case ResponseType::ResponseOf136Bits:
        r.response[0] = m_registers->response_0;
        r.response[1] = m_registers->response_1;
        r.response[2] = m_registers->response_2;
        r.response[3] = m_registers->response_3;
        break;
    case ResponseType::ResponseOf48Bits:
        r.response[0] = m_registers->response_0;
        break;
    case ResponseType::ResponseOf48BitsWithBusy:
        // FIXME: Idk what to do here
        break;
    }

    // 4. Judge whether the command uses the Transfer Complete Interrupt or not.
    // If it uses Transfer
    //    Complete, go to step (5). If not, go to step (7).
    if (command_uses_transfer_complete_interrupt(last_sent_command())) {
        // 5. Wait for the Transfer Complete Interrupt. If the Transfer Complete
        // Interrupt has occurred, go to step (6).
        while ((m_registers->interrupt_status & TRANSFER_COMPLETE) == 0)
            ;

        // 6. Write 1 to Transfer Complete in the Normal Interrupt Status
        // register to clear this bit.
        m_registers->interrupt_status = TRANSFER_COMPLETE;
    }

    // NOTE: Steps 7, 8 and 9 consist of checking the response for errors, which
    // are specific to each command therefore those steps are not implemented
    // here.
    // FIXME: Delete this line? Why is this here?
    m_registers->interrupt_status = 0xffffffff;

    return {r};
}

ErrorOr<void> SDDevice::sd_clock_supply(u64 frequency) {
    // SDHC 3.2.1 SD Clock Supply Sequence
    // The *Clock Control* register is in the lower 16 bits of *Host
    // Configuration 1*
    VERIFY((m_registers->host_configuration_1 & SD_CLOCK_ENABLE) == 0);

    // 1. Find out the divisor to determine the SD Clock Frequency
    const u32 sd_clock_frequency = TRY(retrieve_sd_clock_frequency());

    // FIXME: The way the SD Clock is to be calculated is different for other
    // versions
    VERIFY(host_version() == SDHostVersion::Version3);
    u32 divisor = AK::max(sd_clock_frequency / (frequency), 2);

    if (sd_clock_frequency / divisor >= frequency) {
        divisor++;
    }

    divisor -= 2;

    // 2. Set **Internal Clock Enable** and **SDCLK Frequency Select** in the
    // *Clock Control* register
    const u32 two_upper_bits_of_sdclk_frequency_select = (divisor >> 8 & 0x3)
                                                         << 6;
    const u32 eight_lower_bits_of_sdclk_frequency_select = (divisor & 0xff)
                                                           << 8;
    const u32 SDCLK_FREQUENCY_SELECT =
        two_upper_bits_of_sdclk_frequency_select |
        eight_lower_bits_of_sdclk_frequency_select;
    m_registers->host_configuration_1 = m_registers->host_configuration_1 |
                                        INTERNAL_CLOCK_ENABLE |
                                        SDCLK_FREQUENCY_SELECT;

    // 3. Check **Internal Clock Stable** in the *Clock Control* register until
    // it is 1
    if (!retry_with_timeout(
            [&] {
                return m_registers->host_configuration_1 &
                       INTERNAL_CLOCK_STABLE;
            },
            100)) {
        return EIO;
    }

    // 4. Set **SD Clock Enable** in the *Clock Control* register to 1
    m_registers->host_configuration_1 =
        m_registers->host_configuration_1 | SD_CLOCK_ENABLE;

    return {};
}

void SDDevice::sd_clock_stop() {
    // 3.2.2 SD Clock Stop Sequence

    // 1. Set **SD Clock Enable** in the *Clock Control* register to 0
    m_registers->host_configuration_1 =
        m_registers->host_configuration_1 & ~SD_CLOCK_ENABLE;
}

ErrorOr<void> SDDevice::sd_clock_frequency_change(u64 new_frequency) {
    // 3.2.3 SD Clock Frequency Change Sequence

    // 1. Execute the SD Clock Stop Sequence
    sd_clock_stop();

    // 2. Execute the SD Clock Supply Sequence
    return sd_clock_supply(new_frequency);
}

ErrorOr<void> SDDevice::reset_host_controller() {
    m_registers->host_configuration_0 = 0;
    m_registers->host_configuration_1 =
        m_registers->host_configuration_1 | SOFTWARE_RESET_FOR_ALL;
    if (!retry_with_timeout(
            [&] {
                return (m_registers->host_configuration_1 &
                        SOFTWARE_RESET_FOR_ALL) == 0;
            },
            100)) {
        return EIO;
    }

    return {};
}

ErrorOr<void> SDDevice::sync_data_read_command(CommandIndex command_index,
                                               u32 argument, u32 block_count,
                                               u32 block_size, u8 *out) {
    VERIFY(block_size * block_count % 4 == 0);
    u32 *buffer = (u32 *)out;
    auto command = get_command(command_index);
    // 3.7.2 Transaction Control with Data Transfer Using DAT Line (without DMA)

    // 1. Set the value corresponding to the executed data byte length of one
    // block to Block Size register.
    // 2. Set the value corresponding to the executed data block count to Block
    // Count register in accordance with Table 2-8.
    m_registers->block_size_and_block_count = (block_count << 16) | block_size;

    // 3. Set the argument value to Argument 1 register.
    m_registers->argument_1 = argument;

    // 4. Set the value to the Transfer Mode register. The host driver
    // determines Multi / Single Block
    //    Select, Block Count Enable, Data Transfer Direction, Auto CMD12 Enable
    //    and DMA Enable. Multi / Single Block Select and Block Count Enable are
    //    determined according to Table 2-8. (NOTE: We assume `cmd` already has
    //    the correct flags set)
    // 5. Set the value to Command register.
    m_registers->transfer_mode_and_command = EmmcCommand::to_u32(command);

    // 6. Then, wait for the Command Complete Interrupt.
    if (!retry_with_timeout(
            [&]() { return m_registers->interrupt_status & COMMAND_COMPLETE; },
            100)) {
        return EIO;
    }

    // 7. Write 1 to the Command Complete in the Normal Interrupt Status
    // register for clearing this bit.
    m_registers->interrupt_status = COMMAND_COMPLETE;

    // 8. Read Response register and get necessary information of the issued
    // command
    //    (FIXME: Return the value for better error handling)

    // 9. In the case where this sequence is for write to a card, go to step
    // (10).
    //    In case of read from a card, go to step (14).

    // 17. Repeat until all blocks are received and then go to step (18).
    for (u32 i = 0; i < block_count; i++) {

        // 14. Then wait for the Buffer Read Ready Interrupt.
        if (!retry_with_timeout(
                [&]() {
                    return m_registers->interrupt_status & BUFFER_READ_READY;
                },
                100)) {
            return EIO;
        }

        // 15. Write 1 to the Buffer Read Ready in the Normal Interrupt Status
        // register for clearing this bit.
        m_registers->interrupt_status = BUFFER_READ_READY;

        // 16. Read block data (in according to the number of bytes specified at
        // the step (1)) from the Buffer Data Port register
        for (u32 j = 0; j < block_size / sizeof(u32); j++) {
            buffer[i * block_size + j] = m_registers->buffer_data_port;
        }
    }

    // 18. If this sequence is for Single or Multiple Block Transfer, go to step
    // (19). In case of Infinite Block Transfer, go to step (21)

    // 19. Wait for Transfer Complete Interrupt.
    if (!retry_with_timeout(
            [&]() { return m_registers->interrupt_status & TRANSFER_COMPLETE; },
            100)) {
        return EIO;
    }

    // 20. Write 1 to the Transfer Complete in the Normal Interrupt Status
    // register for clearing this bit
    m_registers->interrupt_status = TRANSFER_COMPLETE;

    return {};
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd0() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::GoIdleState);

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd2() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::AllSendCid);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf136Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd3() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::SendRelativeAddr);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd6() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::AppSetBusWidth);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd7() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::SelectCard);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48BitsWithBusy);
    cmd.crc_enable = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd8() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::SendIfCond);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd17() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::ReadSingleBlock);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;
    // card to host
    cmd.direction = 1;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd18() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::ReadMultipleBlock);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;
    // card to host
    cmd.direction = 1;
    // 1.11 Auto CMD12
    // The Host Driver should set Auto CMD12 Enable in the Transfer
    // Mode register when issuing a multiple block transfer command
    cmd.auto_command = 1;
    cmd.block_count = true;
    cmd.multiblock = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd24() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::WriteSingleBlock);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd25() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::WriteMultipleBlock);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;
    // 1.11 Auto CMD12
    // The Host Driver should set Auto CMD12 Enable in the Transfer
    // Mode register when issuing a multiple block transfer command
    cmd.auto_command = 1;
    cmd.block_count = true;
    cmd.multiblock = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd41() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::AppSendOpCond);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd51() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::AppSendCsr);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.direction = 1;
    cmd.is_data = true;

    return cmd;
}

constexpr SDDevice::EmmcCommand SDDevice::build_cmd55() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(SDDevice::CommandIndex::AppCmd);
    cmd.response_type =
        static_cast<u8>(SDDevice::ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

SDDevice::EmmcCommand const &SDDevice::get_command(CommandIndex index) const {
    static constexpr EmmcCommand commands[]{
        build_cmd0(),  build_cmd2(),  build_cmd3(),  build_cmd6(),
        build_cmd7(),  build_cmd8(),  build_cmd17(), build_cmd18(),
        build_cmd24(), build_cmd25(), build_cmd41(), build_cmd55()};

    for (const auto &cmd : commands) {
        if (cmd.index == static_cast<u8>(index)) {
            return cmd;
        }
    }

    VERIFY_NOT_REACHED();
}

} // namespace Kernel

#include <AK/Format.h>
#include <Kernel/Storage/SD/Commands.h>
#include <Kernel/Storage/SD/SDHostController.h>
#include <Kernel/Storage/StorageManagement.h>
#include <Kernel/Time/TimeManagement.h>
#if ARCH(AARCH64)
#    include <Kernel/Arch/aarch64/RPi/SDHostController.h>
#endif

namespace Kernel {

// References:
// - BCM2835: BCM2835 ARM Peripherals: Addresses of the SD Host Controller
// registers
// - SDHCI: SDHCI Simplified Host Controller Specification Version 3.0
// - PLSS: Physical Layer Simplified Specification Version 9.00

static void delay(i64 nanoseconds)
{
    auto start = TimeManagement::the().monotonic_time().to_nanoseconds();
    auto end = start + nanoseconds;
    while (TimeManagement::the().monotonic_time().to_nanoseconds() < end)
        ;
}

constexpr u32 MAX_SUPPORTED_SDSC_FREQUENCY = 25000000;

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

// PLSS 5.1: all voltage windows
constexpr u32 ACMD41_VOLTAGE = 0x00ff8000;
// check if CCS bit is set => SDHC support
constexpr u32 ACMD41_SDHC = 0x40000000;
// PLSS 4.2.3.1: All voltage windows, XPC = 1, SDHC = 1
constexpr u32 ACMD41_ARG = 0x50ff8000;

SDHostController::SDHostController(u32 hardware_relative_controller_id)
    : StorageController(hardware_relative_controller_id)
{
}

bool SDHostController::reset() { TODO(); }

bool SDHostController::shutdown() { TODO(); }

void SDHostController::complete_current_request(
    AsyncDeviceRequest::RequestResult)
{
    VERIFY_NOT_REACHED();
}

ErrorOr<NonnullLockRefPtr<SDHostController>>
SDHostController::try_initialize()
{
#if ARCH(AARCH64)
    auto hardware_relative_controller_id = StorageManagement::generate_relative_sd_controller_id({});
    auto controller = TRY(adopt_nonnull_lock_ref_or_enomem(
        new RPi::SDHostController(hardware_relative_controller_id)));
    TRY(controller->initialize());

    return { controller };
#else
    return ENODEV;
#endif
}

ErrorOr<void> SDHostController::initialize()
{
    m_registers = get_register_map_base_address();
    if (!m_registers)
        return EIO;

    if (host_version() != SDHostVersion::Version3)
        return ENOTSUP;

    TRY(reset_host_controller());

    m_registers->interrupt_status_enable = 0xffffffff;
    m_registers->interrupt_signal_enable = 0xffffffff;

    m_card = TRY(try_initialize_inserted_card());

    return {};
}

ErrorOr<NonnullLockRefPtr<SDMemoryCard>>
SDHostController::try_initialize_inserted_card()
{
    // PLSS: 4.2 Card Identification Mode
    // After power-on ...the cards are initialized with ... 400KHz clock
    // frequency.
    TRY(sd_clock_supply(400000));

    // PLSS: 4.2.3 Card Initialization and Identification Process
    // Also see Figure 4-2 in the PLSS spec for a flowchart of the
    // initialization process. Note that the steps correspond to the steps in
    // the flowchart, although I made up the numbering and text

    // 1. Send CMD0 (GO_IDLE_STATE) to the card
    TRY(issue_command(SD::CommandIndex::GoIdleState, 0));
    TRY(wait_for_response());

    // 2. Send CMD8 (SEND_IF_COND) to the card
    // SD interface condition: 7:0 = check pattern, 11:8 = supply voltage
    //      0x1aa: check pattern = 10101010, supply voltage = 1 => 2.7-3.6V
    const u32 VOLTAGE_WINDOW = 0x1aa;
    TRY(issue_command(SD::CommandIndex::SendIfCond, VOLTAGE_WINDOW));
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
    SD::OperatingConditionRegister ocr = {};
    bool card_is_usable = true;
    if (!retry_with_timeout(
            [&]() {
                if (issue_command(SD::CommandIndex::AppCmd, 0).is_error() || wait_for_response().is_error())
                    return false;

                if (issue_command(SD::CommandIndex::AppSendOpCond, ACMD41_ARG)
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

                    ocr = SD::OperatingConditionRegister::from_acmd41_response(
                        acmd41_response.value().response[0]);
                }

                return ocr.card_power_up_status == 1;
            })) {
        return card_is_usable ? EIO : ENODEV;
    }

    // 6. If you requested to switch to 1.8V, and the card accepts, execute a
    // voltage switch sequence
    //    (we didn't ask it)

    // 7. Send CMD2 (ALL_SEND_CID) to the card
    TRY(issue_command(SD::CommandIndex::AllSendCid, 0));
    auto all_send_cid_response = TRY(wait_for_response());
    auto cid = SD::CardIdentificationRegister::from_cid_response(
        all_send_cid_response.response);

    // 8. Send CMD3 (SEND_RELATIVE_ADDR) to the card
    TRY(issue_command(SD::CommandIndex::SendRelativeAddr, 0));
    auto send_relative_addr_response = TRY(wait_for_response());
    u32 rca = send_relative_addr_response
                  .response[0]; // FIXME: Might need to clear some bits here

    // PLSS: 5.3 CSD Register
    TRY(issue_command(SD::CommandIndex::SendCsd, rca));
    auto send_csd_response = TRY(wait_for_response());
    auto csd = SD::CardSpecificDataRegister::from_csd_response(
        send_csd_response.response);

    // PLSS 5.3.2 CSD Register (CSD Version 1.0): C_SIZE
    u32 block_count = (csd.device_size + 1) * (1 << (csd.device_size_multiplier + 2));
    u32 block_size = (1 << csd.max_read_data_block_length);
    u64 capacity = (u64)block_count * block_size;
    u64 card_capacity_in_blocks = capacity / block_size;

    dbgln("SD: block_size: {}, block_count: {}, capacity: {}", block_size,
        block_count, capacity);

    // Extra steps:

    // TODO: Do high speed initialisation, if supported
    TRY(sd_clock_frequency_change(MAX_SUPPORTED_SDSC_FREQUENCY));

    TRY(issue_command(SD::CommandIndex::SelectCard, rca));
    TRY(wait_for_response());

    // No SDHC support so manually set block length to 512
    if (!ocr.card_capacity_status) {
        TRY(issue_command(SD::CommandIndex::SetBlockLen, 512));
        TRY(wait_for_response());
    }

    u32 scr_bytes[2];
    TRY(issue_command(SD::CommandIndex::AppCmd, rca));
    TRY(wait_for_response());
    TRY(sync_data_read_command(SD::CommandIndex::AppSendScr, 0, 1, 8,
        (u8*)scr_bytes));
    auto scr = SD::SDConfigurationRegister::from_u64(
        static_cast<u64>(scr_bytes[1]) << 32 | scr_bytes[0]);

    TRY(issue_command(SD::CommandIndex::AppCmd, rca));
    TRY(wait_for_response());
    TRY(issue_command(SD::CommandIndex::AppSetBusWidth,
        0x2)); // 0b00=1 bit bus, 0b10=4 bit bus
    TRY(wait_for_response());

    return TRY(adopt_nonnull_lock_ref_or_enomem(
        new SDMemoryCard(*this,
            // FIXME: Unsure if these 2 params are correct
            StorageDevice::LUNAddress { controller_id(), 0, 0 },
            hardware_relative_controller_id(),
            card_capacity_in_blocks, rca, ocr, cid, scr)));
}

bool SDHostController::retry_with_timeout(Function<bool()> f,
    i64 delay_between_tries)
{
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

ErrorOr<void> SDHostController::issue_command(SD::CommandIndex index,
    u32 argument)
{
    // SDHC 3.7.1 Transaction Control without Data Transfer Using DAT Line
    constexpr u32 COMMAND_INHIBIT = 1 << 1;
    auto cmd = SD::get_command(index);

    // 1. Check Command Inhibit (CMD) in the Present State register.
    //    Repeat this step until **Command Inhibit (CMD)** is 0.
    //    That is, when Command Inhibit (CMD) is 1, the Host Driver
    //    shall not issue an SD Command.
    if (!retry_with_timeout(
            [&]() { return !(m_registers->present_state & COMMAND_INHIBIT); })) {
        return EIO;
    }

    // 2. If the Host Driver issues an SD Command using DAT lines
    //    including busy signal, go to step (3).
    //    If without using DAT lines including busy signal, go to step (5).
    // 3. If the Host Driver is issuing an abort command, go to step (5). In the
    // case of non-abort
    //    command, go to step (4).
    if (cmd.requires_dat_line() && !cmd.is_abort()) {

        // 4. Check Command Inhibit (DAT) in the Present State register. Repeat
        // this step until
        //    Command Inhibit (DAT) is set to 0.
        constexpr u32 DATA_INHIBIT = 1 << 2;
        if (!retry_with_timeout(
                [&]() { return !(m_registers->present_state & DATA_INHIBIT); })) {
            return EIO;
        }
    }

    // 5. Set registers as described in Table 1-2 except Command register.
    m_registers->argument_1 = argument;

    // 6. Set the Command register.
    m_registers->transfer_mode_and_command = cmd.to_u32();

    // 7. Perform Command Completion Sequence in accordance with 3.7.1.2.
    // Done in wait_for_response()

    return {};
}

ErrorOr<SDHostController::Response> SDHostController::wait_for_response()
{
    // SDHC 3.7.1.2 The Sequence to Finalize a Command

    // 1. Wait for the Command Complete Interrupt. If the Command Complete
    // Interrupt has occurred,
    //    go to step (2).
    if (!retry_with_timeout(
            [&]() { return m_registers->interrupt_status & COMMAND_COMPLETE; })) {
        return EIO;
    }

    // 2. Write 1 to Command Complete in the Normal Interrupt Status register to
    // clear this bit
    m_registers->interrupt_status = COMMAND_COMPLETE;

    // 3. Read the Response register(s) to get the response.
    struct Response r = {};
    auto cmd = last_sent_command();
    switch (cmd.expected_response_type()) {
    case SD::ResponseType::NoResponse:
        break;
    case SD::ResponseType::ResponseOf136Bits:
        r.response[0] = m_registers->response_0;
        r.response[1] = m_registers->response_1;
        r.response[2] = m_registers->response_2;
        r.response[3] = m_registers->response_3;
        break;
    case SD::ResponseType::ResponseOf48Bits:
        r.response[0] = m_registers->response_0;
        break;
    case SD::ResponseType::ResponseOf48BitsWithBusy:
        // FIXME: Idk what to do here
        break;
    }

    // 4. Judge whether the command uses the Transfer Complete Interrupt or not.
    // If it uses Transfer
    //    Complete, go to step (5). If not, go to step (7).
    if (last_sent_command().uses_transfer_complete_interrupt()) {
        // 5. Wait for the Transfer Complete Interrupt. If the Transfer Complete
        // Interrupt has occurred, go to step (6).

        while ((m_registers->interrupt_status & TRANSFER_COMPLETE) == 0)
            ;

        // 6. Write 1 to Transfer Complete in the Normal Interrupt Status
        // register to clear this bit.
        m_registers->interrupt_status = TRANSFER_COMPLETE;
    }

    if (cmd.expected_response_type() != SD::ResponseType::ResponseOf136Bits) {
        if (card_status_contains_errors(cmd.command_index(), r.response[0])) {
            return EIO;
        }
    }

    // NOTE: Steps 7, 8 and 9 consist of checking the response for errors, which
    // are specific to each command therefore those steps are not implemented
    // here.

    return { r };
}

ErrorOr<void> SDHostController::sd_clock_supply(u64 frequency)
{
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
    const u32 SDCLK_FREQUENCY_SELECT = two_upper_bits_of_sdclk_frequency_select | eight_lower_bits_of_sdclk_frequency_select;
    m_registers->host_configuration_1 = m_registers->host_configuration_1 | INTERNAL_CLOCK_ENABLE | SDCLK_FREQUENCY_SELECT;

    // 3. Check **Internal Clock Stable** in the *Clock Control* register until
    // it is 1
    if (!retry_with_timeout(
            [&] {
                return m_registers->host_configuration_1 & INTERNAL_CLOCK_STABLE;
            })) {
        return EIO;
    }

    // 4. Set **SD Clock Enable** in the *Clock Control* register to 1
    m_registers->host_configuration_1 = m_registers->host_configuration_1 | SD_CLOCK_ENABLE;

    return {};
}

void SDHostController::sd_clock_stop()
{
    // 3.2.2 SD Clock Stop Sequence

    // 1. Set **SD Clock Enable** in the *Clock Control* register to 0
    m_registers->host_configuration_1 = m_registers->host_configuration_1 & ~SD_CLOCK_ENABLE;
}

ErrorOr<void> SDHostController::sd_clock_frequency_change(u64 new_frequency)
{
    // 3.2.3 SD Clock Frequency Change Sequence

    // 1. Execute the SD Clock Stop Sequence
    sd_clock_stop();

    // 2. Execute the SD Clock Supply Sequence
    return sd_clock_supply(new_frequency);
}

ErrorOr<void> SDHostController::reset_host_controller()
{
    m_registers->host_configuration_0 = 0;
    m_registers->host_configuration_1 = m_registers->host_configuration_1 | SOFTWARE_RESET_FOR_ALL;
    if (!retry_with_timeout(
            [&] {
                return (m_registers->host_configuration_1 & SOFTWARE_RESET_FOR_ALL) == 0;
            })) {
        return EIO;
    }

    return {};
}

ErrorOr<void> SDHostController::sync_data_read_command(SD::CommandIndex index,
    u32 argument,
    u32 block_count,
    u32 block_size,
    u8* out)
{
    VERIFY(block_size * block_count % 4 == 0);
    u32* buffer = (u32*)out;
    auto command = SD::get_command(index);
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
    m_registers->transfer_mode_and_command = command.to_u32();

    // 6. Then, wait for the Command Complete Interrupt.
    if (!retry_with_timeout(
            [&]() { return m_registers->interrupt_status & COMMAND_COMPLETE; })) {
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
                })) {
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
            [&]() { return m_registers->interrupt_status & TRANSFER_COMPLETE; })) {
        return EIO;
    }

    // 20. Write 1 to the Transfer Complete in the Normal Interrupt Status
    // register for clearing this bit
    m_registers->interrupt_status = TRANSFER_COMPLETE;

    return {};
}

ErrorOr<u32> SDHostController::retrieve_sd_clock_frequency()
{
    const i64 ONE_MHZ = 1'000'000;
    const u32 bclock = ((m_registers->capabilities_0 & 0xff00) >> 8) * ONE_MHZ;

    return { bclock };
}

// PLSS Table 4-43 : Card Status Field/Command
bool SDHostController::card_status_contains_errors(SD::CommandIndex index, u32 resp)
{
    SD::CardStatus status;
    // PLSS 4.9.5 R6
    if (index == SD::CommandIndex::SendRelativeAddr) {
        status = SD::CardStatus::from_response((resp & 0x1fff) | ((resp & 0x2000) << 6) | ((resp & 0x4000) << 8) | ((resp & 0x8000) << 8));
    } else {
        status = SD::CardStatus::from_response(resp);
    }

    bool common_errors = status.error || status.cc_error || status.card_ecc_failed || status.illegal_command || status.com_crc_error || status.lock_unlock_failed || status.card_is_locked || status.wp_violation || status.erase_param || status.csd_overwrite;

    bool contains_errors = false;
    switch (index) {
    case SD::CommandIndex::SendRelativeAddr:
        if (status.error || status.illegal_command || status.com_crc_error) {
            contains_errors = true;
        }
        break;
    case SD::CommandIndex::SelectCard:
        if (common_errors) {
            contains_errors = true;
        }
        break;
    case SD::CommandIndex::SetBlockLen:
        if (common_errors || status.block_len_error) {
            contains_errors = true;
        }
        break;
    case SD::CommandIndex::ReadSingleBlock:
    case SD::CommandIndex::ReadMultipleBlock:
        if (common_errors || status.address_error || status.out_of_range) {
            contains_errors = true;
        }
        break;
    case SD::CommandIndex::WriteSingleBlock:
    case SD::CommandIndex::WriteMultipleBlock:
        if (common_errors || status.block_len_error || status.address_error || status.out_of_range) {
            contains_errors = true;
        }
        break;
    case SD::CommandIndex::AppSendScr:
        if (common_errors) {
            contains_errors = true;
        }
        break;
    case SD::CommandIndex::AppCmd:
        if (common_errors) {
            contains_errors = true;
        }
        break;
    default:
        break;
    }

    return contains_errors;
}

} // namespace Kernel

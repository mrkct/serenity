#include <Kernel/Arch/aarch64/RPi/SD.h>
#include <Kernel/Arch/aarch64/RPi/MMIO.h>
#include <Kernel/Arch/aarch64/RPi/GPIO.h>
#include <AK/Format.h>
#include <Kernel/Time/TimeManagement.h>


namespace Kernel::RPi {

const i64 ONE_SECOND = 1'000'000'000;
const i64 TENTH_OF_A_SECOND = ONE_SECOND / 10;

// References:
// - BCM2835: BCM2835 ARM Peripherals: Addresses of the SD Host Controller registers 
// - SDHCI: SDHCI Simplified Host Controller Specification Version 3.0
// - PLSS: Physical Layer Simplified Specification Version 9.00

static void delay(i64 nanoseconds)
{
    auto start = TimeManagement::the().monotonic_time().to_nanoseconds();
    auto end = start + nanoseconds;
    while (TimeManagement::the().monotonic_time().to_nanoseconds() < end);
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

// Commands, defined in PLSS 4.7.4 with the format described in BCM2835 "CMDTM Register"

constexpr u32 GO_IDLE_STATE = 0x00000000;
constexpr u32 SEND_IF_COND = 0x08020000;
constexpr u32 CMD_ALL_SEND_CID = 0x02010000;
constexpr u32 CMD_SEND_REL_ADDR = 0x03020000;
constexpr u32 APP_CMD = 0x37000000;
constexpr u32 APP_SEND_OP_COND = 0x29020000;
constexpr u32 CMD_READ_SINGLE_BLOCK = 0x11220010;
constexpr u32 CMD_SELECT_CARD = 0x07030000;
constexpr u32 APP_SEND_SCR = 0x33220010;
constexpr u32 CMD_SET_BUS_WIDTH = 0x06020000;

bool SD::command_uses_transfer_complete_interrupt(u32) const
{
    // FIXME: I don't know how to determine this.
    return false;
}

bool SD::command_requires_dat_line(u32 command) const
{
    // BCM2835 "CMDTM Register"
    const u32 CMD_ISDATA = 1 << 21;
    return command & CMD_ISDATA;
}

bool SD::command_is_abort(u32 command) const
{
    // BCM2835 "CMDTM Register"
    const u32 CMD_TYPE_MASK  = 0b11 << 22;
    return (command & CMD_TYPE_MASK) == CMD_TYPE_MASK;
}

SD::ResponseType SD::response_type(u32 cmd) const
{
    const u32 CMD_RSPNS_TYPE = 0b11 << 16;
    switch((cmd & CMD_RSPNS_TYPE) >> 16 & 0b11) {
    case 0b00: return ResponseType::NoResponse;
    case 0b01: return ResponseType::ResponseOf136Bits;
    case 0b10: return ResponseType::ResponseOf48Bits;
    case 0b11: return ResponseType::ResponseOf48BitsWithBusy;
    }
    VERIFY_NOT_REACHED();
}

SD& SD::the()
{
    static SD sd;
    return sd;
}

SD::SD()
    : m_registers(MMIO::the().peripheral<SDRegisters>(0x30'0000))
{
    auto& gpio = GPIO::the();
    gpio.set_pin_function(21, GPIO::PinFunction::Alternate3); // CD
    gpio.set_pin_high_detect_enable(21, true);

    gpio.set_pin_function(22, GPIO::PinFunction::Alternate3); // SD1_CLK
    gpio.set_pin_function(23, GPIO::PinFunction::Alternate3); // SD1_CMD

    gpio.set_pin_function(24, GPIO::PinFunction::Alternate3); // SD1_DAT0
    gpio.set_pin_function(25, GPIO::PinFunction::Alternate3); // SD1_DAT1
    gpio.set_pin_function(26, GPIO::PinFunction::Alternate3); // SD1_DAT2
    gpio.set_pin_function(27, GPIO::PinFunction::Alternate3); // SD1_DAT3

    VERIFY(host_version() == SDHostVersion::Version3);
}

SD::MaybeError SD::initialize()
{
    if (host_version() != SDHostVersion::Version3)
        return { CommunicationFailure::UnsupportedHostVersion };
    
    TRY(reset_host_controller());

    // FIXME: Makes sense, but I couldn't find it in the spec.
    m_registers->interrupt_status_enable = 0xffffffff;
    m_registers->interrupt_signal_enable = 0xffffffff;
    // TRY(enable_interrupts_on_card_insertion_and_removal());

    // PLSS: 4.2 Card Identification Mode
    // After power-on ...the cards are initialized with ... 400KHz clock frequency.
    TRY(sd_clock_supply(400000));

    // PLSS: 4.2.3 Card Initialization and Identification Process
    // Also see Figure 4-2 in the PLSS spec for a flowchart of the initialization process.
    // Note that the steps correspond to the steps in the flowchart, although I made up the numbering and text

    // 1. Send CMD0 (GO_IDLE_STATE) to the card
    dbgln("SD: Sending GO_IDLE_STATE...");
    TRY(issue_command(GO_IDLE_STATE, 0));
    dbgln("SD: Waiting for response...");
    TRY(wait_for_response());

    // 2. Send CMD8 (SEND_IF_COND) to the card
    // FIXME: This is not a valid value according to the spec, but it's what is written in the example code and it works?
    dbgln("SD: Sending SEND_IF_COND...");
    const u32 VOLTAGE_WINDOW = 0x1aa;
    TRY(issue_command(SEND_IF_COND, VOLTAGE_WINDOW));
    auto interface_condition_response = wait_for_response();

    // 3. If the card does not respond to CMD8 it means that (Ver2.00 or later SD Memory Card(voltage mismatch) or Ver1.X SD Memory Card or not SD Memory Card)
    if (interface_condition_response.is_error()) {
        // TODO: This is supposed to be the "No Response" branch of the flowchart in Figure 4-2 of the PLSS spec
        return { CommunicationFailure::UnsupportedSDCard };
    }

    // 4. If the card responds to CMD8, but it's not a valid response then the card is not usable
    if (interface_condition_response.value().response[0] != VOLTAGE_WINDOW) {
        // FIXME: We should probably try again with a lower voltage window
        return { CommunicationFailure::UnusableCard };
    }
    dbgln("SD: SEND_IF_COND response: {:#08x}", interface_condition_response.value().response[0]);

    // 5. Send ACMD41 (SEND_OP_COND) with HCS=1 to the card, repeat this until the card is ready or timeout
    m_ocr = {};
    if (!retry_with_timeout([&]() {
        if (issue_command(APP_CMD, 0).is_error() || wait_for_response().is_error())
            return false;

        if (issue_command(APP_SEND_OP_COND, 0x51ff8000).is_error()) return false;

        if (auto acmd41_response = wait_for_response(); !acmd41_response.is_error()) {
            dbgln("SD: ACMD41 response: {:#08x}", acmd41_response.value().response[0]);
            m_ocr = OperatingConditionRegister::from_acmd41_response(acmd41_response.value().response[0]);
        }
        
        return m_ocr.card_power_up_status == 1;
    }, 100)) {
        return { CommunicationFailure::Timeout };
    }

    // 6. If you requested to switch to 1.8V, and the card accepts, execute a voltage switch sequence
    //    (we didn't ask it)

    // 7. Send CMD2 (ALL_SEND_CID) to the card
    TRY(issue_command(CMD_ALL_SEND_CID, 0));
    auto all_send_cid_response = TRY(wait_for_response());
    m_cid = CardIdentificationRegister::from_cid_response(all_send_cid_response.response);

    // 8. Send CMD3 (SEND_RELATIVE_ADDR) to the card
    TRY(issue_command(CMD_SEND_REL_ADDR, 0));
    auto send_relative_addr_response = TRY(wait_for_response());
    m_rca = send_relative_addr_response.response[0];

    // Extra steps:
    TRY(issue_command(CMD_SELECT_CARD, m_rca));
    TRY(wait_for_response());

    u32 scr[2];
    TRY(issue_command(APP_CMD, m_rca));
    TRY(wait_for_response());
    TRY(sync_data_read_command(APP_SEND_SCR, 0, 1, 8, (u8*) scr));
    m_scr = SDConfigurationRegister::from_u64(static_cast<u64>(scr[1]) << 32 | scr[0]);

    dbgln("SD: scr is {:#016x}", (u64*)&scr);
    dbgln("bus widths: {:x}", m_scr.sd_bus_widths);

    TRY(issue_command(APP_CMD, m_rca));
    TRY(wait_for_response());
    TRY(issue_command(CMD_SET_BUS_WIDTH, 0x2)); // 0b00=1 bit bus, 0b10=4 bit bus
    TRY(wait_for_response());

    return {};
}

bool SD::retry_with_timeout(Function<bool()> f, i64 delay_between_tries)
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

Result<u32, SD::CommunicationFailure> SD::retrieve_sd_clock_frequency()
{
    // FIXME: Actually get the frequency either from the capabilities register or from some other source
    // According to very reputable sources(some random guy on the internet), the RPi 3B+ returns 41666666
    return { 41666666 };
}

SD::MaybeError SD::issue_command(u32 cmd, u32 argument)
{
    // SDHC 3.7.1 Transaction Control without Data Transfer Using DAT Line
    constexpr u32 COMMAND_INHIBIT = 1 << 1;

    // 1. Check Command Inhibit (CMD) in the Present State register.
    //    Repeat this step until **Command Inhibit (CMD)** is 0.
    //    That is, when Command Inhibit (CMD) is 1, the Host Driver
    //    shall not issue an SD Command.
    if (!retry_with_timeout([&](){
        return !(m_registers->present_state & COMMAND_INHIBIT);
    }, 100000)) {
        dbgln("SD: Command {} failed because the command inhibit bit is set", cmd);
        return { CommunicationFailure::Timeout };
    }

    // 2. If the Host Driver issues an SD Command using DAT lines
    //    including busy signal, go to step (3).
    //    If without using DAT lines including busy signal, go to step (5).
    // 3. If the Host Driver is issuing an abort command, go to step (5). In the case of non-abort
    //    command, go to step (4).
    if (command_requires_dat_line(cmd) && command_is_abort(cmd)) {

        // 4. Check Command Inhibit (DAT) in the Present State register. Repeat this step until
        //    Command Inhibit (DAT) is set to 0.
        constexpr u32 DATA_INHIBIT = 1 << 2;
        if (!retry_with_timeout([&](){
            return !(m_registers->present_state & DATA_INHIBIT);
        }, 100)) {
            dbgln("SD: Command {} failed because the data inhibit bit is set", cmd);
            return { CommunicationFailure::Timeout };
        }
    }

    // 5. Set registers as described in Table 1-2 except Command register.
    m_registers->argument_1 = argument;
    
    // 6. Set the Command register.
    m_registers->transfer_mode_and_command = cmd;

    // 7. Perform Command Completion Sequence in accordance with 3.7.1.2.
    // Done in wait_for_response()
    
    return {};
}

Result<SD::Response, SD::CommunicationFailure> SD::wait_for_response()
{
    // SDHC 3.7.1.2 The Sequence to Finalize a Command

    // 1. Wait for the Command Complete Interrupt. If the Command Complete Interrupt has occurred,
    //    go to step (2).
    if (!retry_with_timeout([&]() {
        return m_registers->interrupt_status & COMMAND_COMPLETE;
    }, 10000)) {
        dbgln("timed out waiting for response");
        return { CommunicationFailure::Timeout };
    }

    // 2. Write 1 to Command Complete in the Normal Interrupt Status register to clear this bit
    m_registers->interrupt_status = COMMAND_COMPLETE;

    // 3. Read the Response register(s) to get the response.
    struct Response r = {};
    switch (response_type(last_sent_command())) {
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
dbgln("wait_for_response InterruptStatus: {:x}", (u32) m_registers->interrupt_status);
    // 4. Judge whether the command uses the Transfer Complete Interrupt or not. If it uses Transfer
    //    Complete, go to step (5). If not, go to step (7).
    if (command_uses_transfer_complete_interrupt(last_sent_command())) {
        // 5. Wait for the Transfer Complete Interrupt. If the Transfer Complete Interrupt has occurred, go to step (6).
        while((m_registers->interrupt_status & TRANSFER_COMPLETE) == 0);

        // 6. Write 1 to Transfer Complete in the Normal Interrupt Status register to clear this bit.
        m_registers->interrupt_status = TRANSFER_COMPLETE;
    }

    // NOTE: Steps 7, 8 and 9 consist of checking the response for errors, which are specific to each command therefore those steps are not implemented here.
    m_registers->interrupt_status = 0xffffffff;
    return { r };
}

SD::MaybeError SD::wait_until_ready_to_read_data()
{
    const u32 BUFFER_READ_ENABLE = 1 << 11;

    if (!retry_with_timeout([&]() {
        return m_registers->present_state & BUFFER_READ_ENABLE;
    }, 10000)) {
        dbgln("timeout waiting for BUFFER_READ_ENABLE");
        return { SD::CommunicationFailure::Timeout };
    }

    return {};
}

SD::MaybeError SD::sd_clock_supply(u64 frequency)
{
    // SDHC 3.2.1 SD Clock Supply Sequence
    // The *Clock Control* register is in the lower 16 bits of *Host Configuration 1*
    VERIFY((m_registers->host_configuration_1 & SD_CLOCK_ENABLE) == 0);
    
    // 1. Find out the divisor to determine the SD Clock Frequency
    const u32 sd_clock_frequency = TRY(retrieve_sd_clock_frequency());

    // FIXME: The way the SD Clock is to be calculated is different for other versions
    VERIFY(host_version() == SDHostVersion::Version3);
    const u32 divisor = AK::max(sd_clock_frequency / (frequency), 2);

    // 2. Set **Internal Clock Enable** and **SDCLK Frequency Select** in the *Clock Control* register
    const u32 two_upper_bits_of_sdclk_frequency_select = (divisor >> 8 & 0x3) << 6;
    const u32 eight_lower_bits_of_sdclk_frequency_select = (divisor & 0xff) << 8;
    const u32 SDCLK_FREQUENCY_SELECT = two_upper_bits_of_sdclk_frequency_select | eight_lower_bits_of_sdclk_frequency_select;
    m_registers->host_configuration_1 = m_registers->host_configuration_1 | INTERNAL_CLOCK_ENABLE | SDCLK_FREQUENCY_SELECT;

    // 3. Check **Internal Clock Stable** in the *Clock Control* register until it is 1
    if (!retry_with_timeout([&] {
        return m_registers->host_configuration_1 & INTERNAL_CLOCK_STABLE;
    }, 100)) {
        return { SD::CommunicationFailure::Timeout };
    }
   
    // 4. Set **SD Clock Enable** in the *Clock Control* register to 1
    m_registers->host_configuration_1 = m_registers->host_configuration_1 | SD_CLOCK_ENABLE;

    return {};
}

void SD::sd_clock_stop()
{
    // 3.2.2 SD Clock Stop Sequence

    // 1. Set **SD Clock Enable** in the *Clock Control* register to 0
    m_registers->host_configuration_1 = m_registers->host_configuration_1 & ~SD_CLOCK_ENABLE;
}

SD::MaybeError SD::sd_clock_frequency_change(u64 new_frequency)
{
    // 3.2.3 SD Clock Frequency Change Sequence

    // 1. Execute the SD Clock Stop Sequence
    sd_clock_stop();

    // 2. Execute the SD Clock Supply Sequence
    return sd_clock_supply(new_frequency);
}

SD::MaybeError SD::reset_host_controller()
{
    m_registers->host_configuration_0 = 0;
    m_registers->host_configuration_1 = m_registers->host_configuration_1 | SOFTWARE_RESET_FOR_ALL;
    if (!retry_with_timeout([&]{
        return (m_registers->host_configuration_1 & SOFTWARE_RESET_FOR_ALL) == 0;
    }, 100)) {
        return { SD::CommunicationFailure::Timeout };
    }

    return {};
}

SD::MaybeError SD::enable_interrupts_on_card_insertion_and_removal()
{
    // See "3.1 SD Card Detection" in the spec
    u32 r = m_registers->interrupt_status_enable;
    r |= (1 << 6); // Card Insertion Status Enable
    r |= (1 << 7); // Card Removal Status Enable
    m_registers->interrupt_status_enable = r;

    r = m_registers->interrupt_signal_enable;
    r |= (1 << 6); // Card Insertion Signal Enable
    r |= (1 << 7); // Card Removal Signal Enable
    m_registers->interrupt_signal_enable = r;

    return {};
}

SD::MaybeError SD::sync_data_read_command(u32 cmd, u32 argument, u32 block_count, u32 block_size, u8 *b)
{
    VERIFY(block_size * block_count % 4 == 0);
    u32 *buffer = (u32*) b;
    // 3.7.2 Transaction Control with Data Transfer Using DAT Line (without DMA)

    // 1. Set the value corresponding to the executed data byte length of one block to Block Size register.
    // 2. Set the value corresponding to the executed data block count to Block Count register in accordance with Table 2-8.
    m_registers->block_size_and_block_count = (block_count << 16) | block_size;

    // 3. Set the argument value to Argument 1 register.
    m_registers->argument_1 = argument;

    // 4. Set the value to the Transfer Mode register. The host driver determines Multi / Single Block
    //    Select, Block Count Enable, Data Transfer Direction, Auto CMD12 Enable and DMA Enable.
    //    Multi / Single Block Select and Block Count Enable are determined according to Table 2-8.
    //    (NOTE: We assume `cmd` already has the correct flags set) 
    // 5. Set the value to Command register.
    m_registers->transfer_mode_and_command = cmd;

    // 6. Then, wait for the Command Complete Interrupt.
    if (!retry_with_timeout([&]() {
        return m_registers->interrupt_status & COMMAND_COMPLETE;
    }, 100)) {
        dbgln("SD: Timeout waiting for Command Complete Interrupt");
        return { SD::CommunicationFailure::Timeout };
    }

    // 7. Write 1 to the Command Complete in the Normal Interrupt Status register for clearing this bit.
    m_registers->interrupt_status = COMMAND_COMPLETE;

    // 8. Read Response register and get necessary information of the issued command
    //    (FIXME: Return the value for better error handling)

    // 9. In the case where this sequence is for write to a card, go to step (10).
    //    In case of read from a card, go to step (14).

    
    // 17. Repeat until all blocks are received and then go to step (18).
    for (u32 i = 0; i < block_count; i++) {

        // 14. Then wait for the Buffer Read Ready Interrupt.
        if (!retry_with_timeout([&]() {
            return m_registers->interrupt_status & BUFFER_READ_READY;
        }, 100)) {
            return { SD::CommunicationFailure::Timeout };
        }

        // 15. Write 1 to the Buffer Read Ready in the Normal Interrupt Status register for clearing this bit.
        m_registers->interrupt_status = BUFFER_READ_READY;

        // 16. Read block data (in according to the number of bytes specified at the step (1)) from the Buffer Data Port register
        for (u32 j = 0; j < block_size / sizeof(u32); j++) {
            buffer[i * block_size + j] = m_registers->buffer_data_port;
        }
    }

    // 18. If this sequence is for Single or Multiple Block Transfer, go to step (19). In case of Infinite Block Transfer, go to step (21)

    // 19. Wait for Transfer Complete Interrupt.
    if (!retry_with_timeout([&]() {
        return m_registers->interrupt_status & TRANSFER_COMPLETE;
    }, 100)) {
        dbgln("SD: Timeout waiting for Transfer Complete Interrupt");
        dbgln("Interrupt Status: {:x}", (u32) m_registers->interrupt_status);
        dbgln("Present State: {:x}", (u32) m_registers->present_state);
        return { SD::CommunicationFailure::Timeout };
    }

    // 20. Write 1 to the Transfer Complete in the Normal Interrupt Status register for clearing this bit
    m_registers->interrupt_status = TRANSFER_COMPLETE;

    return {};
}

SD::MaybeError SD::testing()
{
    u8 buffer[512];

    TRY(sync_data_read_command(CMD_READ_SINGLE_BLOCK, 0, 1, 512, buffer));
    for (int i = 0; i < 32; i++)
        dbgln("buffer[{}]: {:x} {:c}", i, buffer[i], buffer[i]);

    return {};
}

}

#include <Kernel/Arch/aarch64/RPi/SD.h>
#include <Kernel/Arch/aarch64/RPi/MMIO.h>
#include <Kernel/Arch/aarch64/RPi/GPIO.h>
#include <AK/Format.h>


namespace Kernel::RPi {

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
    VERIFY(reset_host_controller());
    enable_interrupts_on_card_insertion_and_removal();
}

bool SD::reset_host_controller()
{
    // constexpr u32 SRST_HC = 1 << 24; // Reset Complete Host Circuit
    int timeout = 1000;

    // m_registers-> = 0;
    // m_registers->control1 = SRST_HC;
    // while (m_registers->control1 & SRST_HC && timeout > 0) {
    //     timeout--;
    // }

    return timeout > 0;
}

void SD::enable_interrupts_on_card_insertion_and_removal()
{
    // This is step 1 of the "3.1 SD Card Detection" flow of the spec
    u32 r = m_registers->interrupt_status_enable;
    r |= (1 << 6); // Card Insertion Status Enable
    r |= (1 << 7); // Card Removal Status Enable
    m_registers->interrupt_status_enable = r;

    r = m_registers->interrupt_signal_enable;
    r |= (1 << 6); // Card Insertion Signal Enable
    r |= (1 << 7); // Card Removal Signal Enable
    m_registers->interrupt_signal_enable = r;

}

void SD::testing()
{
    dbgln("emmc testing");
    dbgln("card inserted: {}", is_cart_inserted() ? "yes" : "no");

    while(1);
}

}

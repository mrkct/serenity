#include <AK/Format.h>
#include <Kernel/Arch/aarch64/RPi/GPIO.h>
#include <Kernel/Arch/aarch64/RPi/MMIO.h>
#include <Kernel/Arch/aarch64/RPi/SD.h>
#include <Kernel/Storage/SD/SDDevice.h>
#include <Kernel/Time/TimeManagement.h>

namespace Kernel::RPi {

RPiSDCard::RPiSDCard(StorageDevice::LUNAddress lun_addr, u32 hardware_relative_controller_id)
    : SDDevice(lun_addr, hardware_relative_controller_id)
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

    m_registers = MMIO::the().peripheral<SDRegisters>(0x30'0000);
}

ErrorOr<u32> RPiSDCard::retrieve_sd_clock_frequency()
{
    // FIXME: Actually get the frequency either from the capabilities register or from some other source
    // According to very reputable sources(some random guy on the internet), the RPi 3B+ returns 41666666
    const i64 ONE_MHZ = 1'000'000;
    const u32 bclock = ((m_registers->capabilities_0 & 0xff00) >> 8) * ONE_MHZ;
    dbgln("Base clock: {}", bclock);
    return { bclock };
}

}

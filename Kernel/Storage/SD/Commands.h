#pragma once

#include <AK/Types.h>

namespace Kernel::SD {

// Commands, defined in PLSS 4.7.4 with the format described in BCM2835 "CMDTM
// Register"
enum class CommandIndex {
    GoIdleState = 0,
    AllSendCid = 2,
    SendRelativeAddr = 3,
    AppSetBusWidth = 6,
    SelectCard = 7,
    SendIfCond = 8,
    SendCsd = 9,
    ReadSingleBlock = 17,
    ReadMultipleBlock = 18,
    WriteSingleBlock = 24,
    WriteMultipleBlock = 25,
    AppSendOpCond = 41,
    AppSendScr = 51,
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

EmmcCommand const &get_command(CommandIndex);

enum class ResponseType {
    NoResponse,
    ResponseOf136Bits,
    ResponseOf48Bits,
    ResponseOf48BitsWithBusy
};

} // namespace Kernel::SD

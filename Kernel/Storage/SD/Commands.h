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
    SetBlockLen = 16,
    ReadSingleBlock = 17,
    ReadMultipleBlock = 18,
    WriteSingleBlock = 24,
    WriteMultipleBlock = 25,
    AppSendOpCond = 41,
    AppSendScr = 51,
    AppCmd = 55,
};

enum class CommandType { Normal,
    Suspend,
    Resume,
    Abort };

enum class ResponseType {
    NoResponse,
    ResponseOf136Bits,
    ResponseOf48Bits,
    ResponseOf48BitsWithBusy
};
struct Command {
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

    static Command from_u32(u32 value)
    {
        union {
            u32 x;
            struct Command cmd;
        } u;
        u.x = value;
        return u.cmd;
    }

    u32 to_u32() const
    {
        union {
            u32 x;
            Command cmd;
        } u;
        u.cmd = *this;

        return u.x;
    }

    bool is_abort() const
    {
        // BCM2835 "CMDTM Register"
        return type == static_cast<u8>(SD::CommandType::Abort);
    }

    bool requires_dat_line() const
    {
        // BCM2835 "CMDTM Register"
        return is_data;
    }

    ResponseType expected_response_type() const
    {
        switch (response_type) {
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

    CommandIndex command_index() const
    {
        switch (index) {
        case 0:
            return CommandIndex::GoIdleState;
        case 2:
            return CommandIndex::AllSendCid;
        case 3:
            return CommandIndex::SendRelativeAddr;
        case 6:
            return CommandIndex::AppSetBusWidth;
        case 7:
            return CommandIndex::SelectCard;
        case 8:
            return CommandIndex::SendIfCond;
        case 9:
            return CommandIndex::SendCsd;
        case 16:
            return CommandIndex::SetBlockLen;
        case 17:
            return CommandIndex::ReadSingleBlock;
        case 18:
            return CommandIndex::ReadMultipleBlock;
        case 24:
            return CommandIndex::WriteSingleBlock;
        case 25:
            return CommandIndex::WriteMultipleBlock;
        case 41:
            return CommandIndex::AppSendOpCond;
        case 51:
            return CommandIndex::AppSendScr;
        case 55:
            return CommandIndex::AppCmd;
        }
        VERIFY_NOT_REACHED();
    }

    bool uses_transfer_complete_interrupt() const
    {
        // FIXME: I don't know how to determine this.
        //      probably sth about: TM_AUTO_CMD_EN?
        return false;
    }
} __attribute__((packed));
static_assert(sizeof(Command) == sizeof(u32));

Command const& get_command(CommandIndex);

} // namespace Kernel::SD

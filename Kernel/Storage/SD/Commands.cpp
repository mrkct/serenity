#include <Kernel/Storage/SD/Commands.h>

namespace Kernel::SD {

constexpr Command build_cmd0()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::GoIdleState);

    return cmd;
}

constexpr Command build_cmd2()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AllSendCid);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf136Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd3()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SendRelativeAddr);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd6()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppSetBusWidth);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);

    return cmd;
}

constexpr Command build_cmd7()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SelectCard);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48BitsWithBusy);
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd8()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SendIfCond);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd9()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SendCsd);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf136Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd16()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SetBlockLen);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd17()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::ReadSingleBlock);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;
    // card to host
    cmd.direction = 1;

    return cmd;
}

constexpr Command build_cmd18()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::ReadMultipleBlock);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
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

constexpr Command build_cmd24()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::WriteSingleBlock);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;

    return cmd;
}

constexpr Command build_cmd25()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::WriteMultipleBlock);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
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

constexpr Command build_cmd41()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppSendOpCond);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);

    return cmd;
}

constexpr Command build_cmd51()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppSendScr);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.direction = 1;
    cmd.is_data = true;

    return cmd;
}

constexpr Command build_cmd55()
{
    Command cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppCmd);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

Command const& get_command(CommandIndex index)
{
    static constexpr Command commands[] {
        build_cmd0(), build_cmd2(), build_cmd3(), build_cmd6(),
        build_cmd7(), build_cmd8(), build_cmd9(), build_cmd16(), build_cmd17(),
        build_cmd18(), build_cmd24(), build_cmd25(), build_cmd41(),
        build_cmd51(), build_cmd55()
    };

    for (auto const& cmd : commands) {
        if (cmd.index == static_cast<u8>(index)) {
            return cmd;
        }
    }

    VERIFY_NOT_REACHED();
}

} // namespace Kernel::SD

#include <Kernel/Storage/SD/Commands.h>

namespace Kernel::SD {

constexpr EmmcCommand build_cmd0() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::GoIdleState);

    return cmd;
}

constexpr EmmcCommand build_cmd2() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AllSendCid);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf136Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr EmmcCommand build_cmd3() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SendRelativeAddr);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr EmmcCommand build_cmd6() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppSetBusWidth);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);

    return cmd;
}

constexpr EmmcCommand build_cmd7() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SelectCard);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48BitsWithBusy);
    cmd.crc_enable = true;

    return cmd;
}

constexpr EmmcCommand build_cmd8() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SendIfCond);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr EmmcCommand build_cmd9() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::SendCsd);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf136Bits);
    cmd.crc_enable = true;

    return cmd;
}

constexpr EmmcCommand build_cmd17() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::ReadSingleBlock);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;
    // card to host
    cmd.direction = 1;

    return cmd;
}

constexpr EmmcCommand build_cmd18() {
    EmmcCommand cmd = {};
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

constexpr EmmcCommand build_cmd24() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::WriteSingleBlock);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.is_data = true;
    cmd.crc_enable = true;

    return cmd;
}

constexpr EmmcCommand build_cmd25() {
    EmmcCommand cmd = {};
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

constexpr EmmcCommand build_cmd41() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppSendOpCond);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);

    return cmd;
}

constexpr EmmcCommand build_cmd51() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppSendScr);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.direction = 1;
    cmd.is_data = true;

    return cmd;
}

constexpr EmmcCommand build_cmd55() {
    EmmcCommand cmd = {};
    cmd.index = static_cast<u8>(CommandIndex::AppCmd);
    cmd.response_type = static_cast<u8>(ResponseType::ResponseOf48Bits);
    cmd.crc_enable = true;

    return cmd;
}

EmmcCommand const &get_command(CommandIndex index) {
    static constexpr EmmcCommand commands[]{
        build_cmd0(),  build_cmd2(),  build_cmd3(),  build_cmd6(),
        build_cmd7(),  build_cmd8(),  build_cmd9(),  build_cmd17(),
        build_cmd18(), build_cmd24(), build_cmd25(), build_cmd41(),
        build_cmd51(), build_cmd55()};

    for (auto const &cmd : commands) {
        if (cmd.index == static_cast<u8>(index)) {
            return cmd;
        }
    }

    VERIFY_NOT_REACHED();
}

} // namespace Kernel::SD

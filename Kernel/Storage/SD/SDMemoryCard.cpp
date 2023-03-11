#include <Kernel/Storage/SD/Commands.h>
#include <Kernel/Storage/SD/SDHostController.h>
#include <Kernel/Storage/SD/SDMemoryCard.h>

namespace Kernel {

SDMemoryCard::SDMemoryCard(
    SDHostController& sdhc,
    StorageDevice::LUNAddress lun_address,
    u32 hardware_relative_controller_id,
    u64 capacity_in_blocks,
    u32 relative_card_address,
    SD::OperatingConditionRegister ocr,
    SD::CardIdentificationRegister cid,
    SD::SDConfigurationRegister scr)
    : StorageDevice(lun_address, hardware_relative_controller_id, 512, capacity_in_blocks)
    , m_sdhc(sdhc)
    , m_relative_card_address(relative_card_address)
    , m_ocr(ocr)
    , m_cid(cid)
    , m_scr(scr)
{
}

void SDMemoryCard::start_request(AsyncBlockDeviceRequest& request)
{
    MutexLocker locker(m_lock);

    // FIXME: Check if the card was removed and notify the host controller
    VERIFY(m_sdhc.is_card_inserted());

    if (request.request_type() == AsyncBlockDeviceRequest::RequestType::Write) {
        TODO();
    }

    VERIFY(request.block_size() <= 512);
    u8 data[512]; // FIXME: Horrible
    for (u32 block = 0; block < request.block_count(); ++block) {
        u32 offset = request.block_index() + block;
        if (card_addressing_mode() == CardAddressingMode::ByteAddressing)
            offset *= 512;

        if (m_sdhc.sync_data_read_command(SD::CommandIndex::ReadSingleBlock, offset, 1, 512, data).is_error()) {
            request.complete(AsyncDeviceRequest::Failure);
            return;
        }

        MUST(request.buffer().write(data, block * request.block_size(), request.block_size()));
    }

    request.complete(AsyncDeviceRequest::Success);
}

}

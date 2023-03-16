/*
 * Copyright (c) 2023, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Storage/SD/Commands.h>
#include <Kernel/Storage/SD/SDHostController.h>
#include <Kernel/Storage/SD/SDMemoryCard.h>

namespace Kernel {

SDMemoryCard::SDMemoryCard(SDHostController& sdhc,
    StorageDevice::LUNAddress lun_address,
    u32 hardware_relative_controller_id,
    u64 capacity_in_blocks, u32 relative_card_address,
    SD::OperatingConditionRegister ocr,
    SD::CardIdentificationRegister cid,
    SD::SDConfigurationRegister scr)
    : StorageDevice(lun_address, hardware_relative_controller_id, 512,
        capacity_in_blocks)
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

    VERIFY(request.block_size() == 512);

    // FIXME: Check if the card was removed and notify the host controller
    VERIFY(m_sdhc.is_card_inserted());

    auto buffer = request.buffer();
    u32 block_address = request.block_index();
    u32 block_increment = 1;
    if (card_addressing_mode() == CardAddressingMode::ByteAddressing) {
        block_address *= 512;
        block_increment *= 512;
    }

    if (request.request_type() == AsyncBlockDeviceRequest::RequestType::Write) {
        for (u32 block = 0; block < request.block_count(); ++block) {
            if (m_sdhc.write_block(block_address, buffer).is_error()) {
                request.complete(AsyncDeviceRequest::Failure);
                return;
            }
            buffer = buffer.offset(request.block_size());
            block_address += block_increment;
        }
    } else {
        for (u32 block = 0; block < request.block_count(); ++block) {
            if (m_sdhc.read_block(block_address, buffer).is_error()) {
                request.complete(AsyncDeviceRequest::Failure);
                return;
            }
            buffer = buffer.offset(request.block_size());
            block_address += block_increment;
        }
    }

    request.complete(AsyncDeviceRequest::Success);
}

}

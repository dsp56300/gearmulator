#include "88lib/rom/romloader.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

#include "baseLib/filesystem.h"
#include "common/romDescramble.h"

namespace emu88Lib
{
    namespace
    {
        Rom readRom(const RomInventory& _inventory, const RomDevice _device, const RomSlot _slot)
        {
            const auto* found = _inventory.find(_device, _slot);
            std::vector<uint8_t> data;
            if (!found || !_inventory.read(data, _device, _slot))
                return {};
            if (found->entry)
                return {std::move(data), found->entry};
            const auto model = _device == RomDevice::Sc88VL ? Model::Sc88VL
                : _device == RomDevice::Xpgs                ? Model::Xpgs
                                                            : Model::Sc88;
            return {std::move(data), baseLib::filesystem::getFilenameWithoutPath(found->path), model};
        }
    } // namespace
    bool RomLoader::isDeviceAvailable(const RomDevice _device) { return scan().isComplete(_device); }

    bool RomLoader::isDeviceAvailable(const DeviceModel _model) { return isDeviceAvailable(toRomDevice(_model)); }

    RomDevice RomLoader::toRomDevice(const DeviceModel _model)
    {
        switch (_model)
        {
        case DeviceModel::Xpgs:
            return RomDevice::Xpgs;
        case DeviceModel::Sc8820:
            return RomDevice::Sc8820;
        case DeviceModel::Cm32p:
            return RomDevice::Cm32p;
        case DeviceModel::Cm32l:
            return RomDevice::Cm32l;
        case DeviceModel::Cm64:
            return RomDevice::Cm64;
        case DeviceModel::Sc88:
            return RomDevice::Sc88;
        case DeviceModel::Sc88VL:
            return RomDevice::Sc88VL;
        case DeviceModel::Sc88Pro:
            return RomDevice::Sc88Pro;
        case DeviceModel::VeGsPro:
            return RomDevice::VeGsPro;
        case DeviceModel::Sc8850:
            return RomDevice::Sc8850;
        case DeviceModel::Sc55Mk2:
            return RomDevice::Sc55Mk2;
        case DeviceModel::Sc55Mk1:
            return RomDevice::Sc55Mk1;
        case DeviceModel::Sc55St:
            return RomDevice::Sc55St;
        case DeviceModel::Cm300:
            return RomDevice::Cm300;
        case DeviceModel::Scc1a:
            return RomDevice::Scc1a;
        case DeviceModel::Scb55:
            return RomDevice::Scb55;
        case DeviceModel::Rlp3237:
            return RomDevice::Rlp3237;
        case DeviceModel::Sc155:
            return RomDevice::Sc155;
        case DeviceModel::Sc155Mk2:
            return RomDevice::Sc155Mk2;
        }
        return RomDevice::Sc88;
    }

    Rom RomLoader::findROM(const Model _model)
    {
        const auto device = _model == Model::Sc88VL ? RomDevice::Sc88VL
            : _model == Model::Xpgs                 ? RomDevice::Xpgs
                                                    : RomDevice::Sc88;
        return readRom(scan(), device, RomSlot::Control);
    }

    Sc88ProRomSet RomLoader::findSc88ProRomSet(const RomDevice _device)
    {
        const auto inventory = scan();
        if ((_device != RomDevice::Sc88Pro && _device != RomDevice::VeGsPro) || !inventory.isComplete(_device))
            return {};

        Sc88ProRomSet result;
        if (!inventory.read(result.firmware, _device, RomSlot::Control) ||
            !inventory.read(result.waveA, _device, RomSlot::Wave, 0) ||
            !inventory.read(result.waveB, _device, RomSlot::Wave, 1) ||
            !inventory.read(result.waveC, _device, RomSlot::Wave, 2))
            return {};

        if (const auto* found = inventory.find(_device, RomSlot::Control))
            result.firmwareName =
                found->entry ? describe(*found->entry) : baseLib::filesystem::getFilenameWithoutPath(found->path);

        return result.isValid() ? result : Sc88ProRomSet{};
    }

    Sc8850RomSet RomLoader::findSc8850RomSet()
    {
        const auto inventory = scan();

        Sc8850RomSet result;
        if (!inventory.read(result.cpu, RomDevice::Sc8850, RomSlot::Internal) ||
            !inventory.read(result.program, RomDevice::Sc8850, RomSlot::Program) ||
            !inventory.read(result.data, RomDevice::Sc8850, RomSlot::Data))
            return {};

        return result.isValid() ? result : Sc8850RomSet{};
    }

    Sc8850WaveRomSet RomLoader::findSc8850WaveRomSet()
    {
        Sc8850WaveRomSet result;
        const auto inventory = scan();
        if (!inventory.read(result.romA, RomDevice::Sc8850, RomSlot::Wave, 0) ||
            !inventory.read(result.romB, RomDevice::Sc8850, RomSlot::Wave, 1))
            return {};
        return result.isValid() ? result : Sc8850WaveRomSet{};
    }

    Sc55RomSet RomLoader::findSc55RomSet(const DeviceModel _model)
    {
        const auto inventory = scan();
        const auto device = toRomDevice(_model);

        Sc55RomSet result;
        result.model = _model;
        if (!inventory.read(result.internalRom, device, RomSlot::Internal) ||
            !inventory.read(result.programRom, device, RomSlot::Program))
            return {};
        const auto profile = result.profile();
        for (uint8_t i = 0; i < result.waveRom.size(); ++i)
            if (profile.waveSizes[i] != 0 && !inventory.read(result.waveRom[i], device, RomSlot::Wave, i))
                return {};

        return result.isValid() ? result : Sc55RomSet{};
    }

    Cm32pRomSet RomLoader::findCm32pRomSet()
    {
        const auto inventory = scan();
        Cm32pRomSet result;
        if (!inventory.read(result.program, RomDevice::Cm32p, RomSlot::Program))
            return {};
        for (uint8_t i = 0; i < result.waves.size(); ++i)
            if (!inventory.read(result.waves[i], RomDevice::Cm32p, RomSlot::Wave, i))
                return {};
        return result.isValid() ? result : Cm32pRomSet{};
    }

    Cm32lRomSet RomLoader::findCm32lRomSet()
    {
        const auto inventory = scan();
        Cm32lRomSet result;
        if (!inventory.read(result.control, RomDevice::Cm32l, RomSlot::Control) ||
            !inventory.read(result.wave, RomDevice::Cm32l, RomSlot::Wave) ||
            !inventory.read(result.reverb, RomDevice::Cm32l, RomSlot::Reverb))
            return {};
        return result.isValid() ? result : Cm32lRomSet{};
    }

    WaveRom RomLoader::findWaveRom()
    {
        const auto inventory = scan();

        std::array<std::vector<uint8_t>, WaveRom::ChipCount> chips;
        for (uint8_t bank = 0; bank < WaveRom::ChipCount; ++bank)
            inventory.read(chips[bank], RomDevice::Sc88, RomSlot::Wave, bank);

        return WaveRom(chips);
    }
    std::vector<uint8_t> RomLoader::findXpgsWaveRom()
    {
        auto waves = findWaveRom();
        std::vector<uint8_t> extension;
        if (!waves.isValid() || !scan().read(extension, RomDevice::Xpgs, RomSlot::Wave, 4))
            return {};
        auto data = waves.takeData();
        const auto offset = data.size();
        data.resize(offset + extension.size());
        WaveRom::unscramble(extension.data(), extension.size(), data.data() + offset, extension.size());
        return data;
    }

} // namespace emu88Lib

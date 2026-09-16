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
        void swapWords(std::vector<uint8_t>& _data)
        {
            for (size_t i = 0; i + 1 < _data.size(); i += 2)
                std::swap(_data[i], _data[i + 1]);
        }

        // The SC-88Pro and the VE-GS Pro are one board with different control ROMs:
        // they share the wave set, its SC-8820/8850 donors and the oversized dumps.
        constexpr bool usesProBoard(const RomDevice _device)
        {
            return _device == RomDevice::Sc88Pro || _device == RomDevice::VeGsPro;
        }

        // Inverse of WaveRom::unscramble. Pro ROM sets contain physical chip data,
        // whereas the registered SC-8820/8850 dumps are already decoded. Every
        // slice here starts on a 1 MiB boundary and contains complete blocks.
        std::vector<uint8_t> scrambleProWave(const uint8_t* _decoded, const size_t _size)
        {
            std::vector<uint8_t> raw(_size);
            rLib::rom::Pcm16::scramble(_decoded, _size, raw.data());
            return raw;
        }

        std::vector<std::pair<RomSlot, uint8_t>> requiredSlots(const RomDevice _device)
        {
            std::vector<std::pair<RomSlot, uint8_t>> result;
            for (const auto& spec : g_romFileSpecs)
            {
                if (spec.device != _device)
                    continue;
                const std::pair<RomSlot, uint8_t> slot{spec.slot, spec.index};
                if (std::find(result.begin(), result.end(), slot) == result.end())
                    result.push_back(slot);
            }
            if (!result.empty())
                return result;
            for (const auto& entry : g_romRegistry)
            {
                if (!usedBy(entry, _device))
                    continue;
                const std::pair<RomSlot, uint8_t> slot{entry.slot, entry.index};
                if (std::find(result.begin(), result.end(), slot) == result.end())
                    result.push_back(slot);
            }
            return result;
        }


    } // namespace
    const FoundRom* RomInventory::find(const RomDevice _device, const RomSlot _slot, const uint8_t _index) const
    {
        for (const auto& rom : m_roms)
            if (rom.namedSpec && rom.slot() == _slot && rom.index() == _index && rom.usedBy(_device))
                return &rom;

        const FoundRom* result = nullptr;

        for (const auto& rom : m_roms)
        {
            if (!rom.entry || rom.entry->slot != _slot || rom.entry->index != _index ||
                !emu88Lib::usedBy(*rom.entry, _device))
                continue;
            if (!result || rom.entry < result->entry)
                result = &rom;
        }
        return result;
    }

    const FoundRom* RomInventory::findSc88ProWaveSource(const uint8_t _index, size_t& _offset) const
    {
        if (_index > 2)
            return nullptr;

        constexpr size_t offsets[] = {0, Sc88ProRomSet::WaveASize, Sc88ProRomSet::WaveASize + Sc88ProRomSet::WaveBSize};
        _offset = offsets[_index];
        if (const auto* source = find(RomDevice::Sc8850, RomSlot::Wave))
            if (_index != 2 || source->size() == Sc8850WaveRomSet::Size)
                return source;
        if (_index == 2)
        {
            _offset = 0;
            if (const auto* source = find(RomDevice::Sc8850, RomSlot::Wave, 1))
                return source;
        }

        // CS0 contains A+B; the first 4 MiB of CS1 contains C. No donor
        // firmware is needed, and native Pro chips may fill the other slots.
        if (_index == 2)
            _offset = 0;
        return find(RomDevice::Sc8820, RomSlot::Wave, _index == 2 ? 1 : 0);
    }

    bool RomInventory::has(const RomDevice _device, const RomSlot _slot, const uint8_t _index) const
    {
        if (find(_device, _slot, _index))
            return true;
        if (_device == RomDevice::Sc8850 && _slot == RomSlot::Wave && _index == 1)
            if (const auto* source = find(_device, _slot, 0); source && source->size() == Sc8850WaveRomSet::Size)
                return true;
        size_t offset = 0;
        return usesProBoard(_device) && _slot == RomSlot::Wave && findSc88ProWaveSource(_index, offset) != nullptr;
    }

    bool RomInventory::read(std::vector<uint8_t>& _data, const RomDevice _device, const RomSlot _slot,
                            const uint8_t _index) const
    {
        const auto* found = find(_device, _slot, _index);
        size_t sourceOffset = 0;
        const bool deriveProWave = !found && usesProBoard(_device) && _slot == RomSlot::Wave;
        if (deriveProWave)
            found = findSc88ProWaveSource(_index, sourceOffset);
        if (!found && _device == RomDevice::Sc8850 && _slot == RomSlot::Wave && _index == 1)
        {
            found = find(_device, _slot, 0);
            if (found && found->size() != Sc8850WaveRomSet::Size)
                found = nullptr;
        }
        if (!found)
            return false;

        // Combined canonical dumps can supply each chip without loading both banks.
        if (_device == RomDevice::Sc8850 && _slot == RomSlot::Wave && found->size() == Sc8850WaveRomSet::Size &&
            !found->embedded && !found->needsWordSwap && !(found->entry && found->entry->xpWaveDump))
        {
            if (_index > 1 || baseLib::filesystem::getFileSize(found->path) != found->size() ||
                !baseLib::filesystem::readFileRegion(_data, found->path, _index * Sc8850WaveRomSet::ChipSize,
                                                     Sc8850WaveRomSet::ChipSize))
            {
                _data.clear();
                return false;
            }
            return true;
        }

        const bool read = found->embedded
            ? baseLib::filesystem::readFileRegion(_data, found->path, found->fileOffset, found->size())
            : baseLib::filesystem::readFile(_data, found->path);
        if (!read || _data.size() != found->size())
        {
            _data.clear();
            return false;
        }

        if (found->needsWordSwap)
            swapWords(_data);
        else if (found->namedSpec && found->namedSpec->normalizeH8Words && !found->entry)
            normalizeH8WordOrder(_data);
        if (found->embedded && (!found->entry || baseLib::MD5(_data) != found->entry->hash))
        {
            _data.clear();
            return false;
        }
        if (found->entry && found->entry->xpWaveDump)
            _data = WaveRom::decodeXpWaveDump(_data);
        if (_device == RomDevice::Sc8850 && _slot == RomSlot::Wave && _data.size() == Sc8850WaveRomSet::Size)
        {
            const auto first = _data.begin() + _index * Sc8850WaveRomSet::ChipSize;
            _data = std::vector<uint8_t>(first, first + Sc8850WaveRomSet::ChipSize);
        }
        if (deriveProWave)
        {
            const size_t size = _index == 2 ? Sc88ProRomSet::WaveCSize : Sc88ProRomSet::WaveASize;
            if (sourceOffset + size > _data.size())
            {
                _data.clear();
                return false;
            }
            _data = scrambleProWave(_data.data() + sourceOffset, size);
            const auto* entry = findRegistryEntry(size, baseLib::MD5(_data));
            if (!entry || !usedBy(*entry, _device) || entry->slot != RomSlot::Wave || entry->index != _index)
            {
                _data.clear();
                return false;
            }
        }
        return true;
    }

    bool RomInventory::isComplete(const RomDevice _device) const
    {
        const auto slots = requiredSlots(_device);
        if (slots.empty())
            return false;

        for (const auto& [slot, index] : slots)
        {
            if (!has(_device, slot, index))
                return false;
        }
        return true;
    }

    std::vector<const RomRegistryEntry*> RomInventory::missing(const RomDevice _device) const
    {
        std::vector<const RomRegistryEntry*> result;
        std::vector<std::pair<RomSlot, uint8_t>> reported;

        for (const auto& entry : g_romRegistry)
        {
            if (!usedBy(entry, _device) || has(_device, entry.slot, entry.index))
                continue;

            const std::pair<RomSlot, uint8_t> slot{entry.slot, entry.index};
            if (std::find(reported.begin(), reported.end(), slot) != reported.end())
                continue;

            reported.push_back(slot);
            result.push_back(&entry);
        }
        return result;
    }

    std::vector<const RomFileSpec*> RomInventory::missingFiles(const RomDevice _device) const
    {
        std::vector<const RomFileSpec*> result;
        for (const auto& spec : g_romFileSpecs)
        {
            if (spec.device != _device || has(_device, spec.slot, spec.index))
                continue;
            result.push_back(&spec);
        }
        return result;
    }

    std::string RomInventory::warnings(const RomDevice device, bool includeCustom) const
    {
        std::ostringstream result;
        for (const auto& rom : m_roms)
        {
            if (!rom.usedBy(device) || find(device, rom.slot(), rom.index()) != &rom)
                continue;
            if (rom.entry && rom.entry->badDump)
                result << rom.path << ": " << rom.entry->version << ". This is not an original CPU dump.\n";
            else if (includeCustom && rom.hashMismatch)
                result << rom.path << ": custom ROM, MD5 " << rom.actualHash.toString()
                       << " does not match a known dump.\n";
            else if (includeCustom && !rom.entry)
                result << rom.path << ": unverified ROM, MD5 " << rom.actualHash.toString()
                       << ". No reference hash is available for this component; accepted by filename and size.\n";
        }
        return result.str();
    }


    std::string RomInventory::describeRequirements(const RomDevice _device) const
    {
        std::ostringstream text;
        text << "A known MD5 is accepted under any filename. Alternatively, use one of the filenames below "
                "with the exact size shown. Custom images are unverified.\n\n";
        std::set<std::pair<RomSlot, uint8_t>> slots;
        for (const auto& spec : g_romFileSpecs)
            if (spec.device == _device)
                slots.emplace(spec.slot, spec.index);
        for (const auto& entry : g_romRegistry)
            if (usedBy(entry, _device))
                slots.emplace(entry.slot, entry.index);

        for (const auto& [slot, index] : slots)
        {
            const auto* found = find(_device, slot, index);
            text << toString(slot);
            if (slot == RomSlot::Wave)
                text << ' ' << (index + 1);
            text << " — "
                 << (found                           ? (found->entry ? "Found" : "Found (unverified)")
                         : has(_device, slot, index) ? "Available from compatible waves"
                                                     : "Missing")
                 << '\n';
            if (found)
                text << "Using: " << baseLib::filesystem::getFilenameWithoutPath(found->path) << '\n';
            for (const auto& spec : g_romFileSpecs)
                if (spec.device == _device && spec.slot == slot && spec.index == index)
                    text << "Filename: " << spec.filename << " (" << spec.size << " bytes / " << (spec.size >> 10)
                         << " KiB)\n";
            bool known = false;
            for (const auto& entry : g_romRegistry)
                if (usedBy(entry, _device) && entry.slot == slot && entry.index == index)
                {
                    text << "MD5: " << entry.hash.toString() << " — " << entry.version << '\n';
                    known = true;
                }
            if (!known)
                text << "No reference hash is registered for this component. Filename and size are required.\n";
            text << '\n';
        }

        if (_device == RomDevice::Sc8820)
            text << "Wave alternatives: SCCore.dll / SCCore00.dylib can supply both decoded wave regions. "
                    "The CPU and program ROMs are still required separately. "
                    "The registered CPU image is a reconstructed BAD_DUMP, not an original dump.\n";
        if (_device == RomDevice::Sc8850)
            text << "Wave alternatives: the 32 MiB SC8850WaveROMUnscrambled.bin supplies both banks. "
                    "Otherwise provide two distinct 16 MiB banks, decoded or in a recognized XP dump layout. "
                    "Two copies of bank A do not supply bank B, regardless of their filenames. "
                    "SCCore supplies bank A only; its second wave region is not SC-8850 bank B.\n";

        if (usesProBoard(_device))
        {
            text << "Compatible alternatives\n"
                    "Control: one of the control MD5s above. "
                    "Recognized 1 MiB copies in 2/4 MiB repeated dumps are also accepted. "
                 << (_device == RomDevice::Sc88Pro
                         ? "The VE-GS Pro's headless program ROM (R01780078) runs as its own device.\n\n"
                         : "SC-88Pro control ROMs run as the SC-88Pro device.\n\n")
                 << "Waves: use the native A+B+C files above, or decoded SC-8850 waves (32 MiB), "
                    "or decoded SC-8820 waves (16+8 MiB). These are alternatives, not additional required files.\n";
            for (const auto& entry : g_romRegistry)
                if (entry.slot == RomSlot::Wave &&
                    (usedBy(entry, RomDevice::Sc8850) || usedBy(entry, RomDevice::Sc8820)))
                    text << toString(usedBy(entry, RomDevice::Sc8850) ? RomDevice::Sc8850 : RomDevice::Sc8820) << " "
                         << entry.version << " (" << (entry.size >> 20) << " MiB), MD5: " << entry.hash.toString()
                         << '\n';
            text << "\nSCCore.dll / SCCore00.dylib can supply the SC-8820 wave regions without manual extraction. "
                    "They supply waves only; a separate control ROM is still required.\n";
        }
        return text.str();
    }


} // namespace emu88Lib

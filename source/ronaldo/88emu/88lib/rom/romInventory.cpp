#include "88lib/rom/romloader.h"

#include <algorithm>
#include <array>
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

        // The CM-64 is a CM-32L and a CM-32P in one case and needs both boards' images. It has
        // no registry rows of its own; everything asked of it is asked of its two halves.
        constexpr std::array<RomDevice, 2> g_cm64Halves{RomDevice::Cm32l, RomDevice::Cm32p};

        // A composite slot's chips are an alternative to its whole image, never an
        // additional requirement: has() reports the whole as available once both chips
        // are there, and each chip as available once the whole is.
        std::vector<std::pair<RomSlot, uint8_t>> requiredSlots(const RomDevice _device)
        {
            std::vector<std::pair<RomSlot, uint8_t>> result;
            const auto add = [&](const RomSlot _slot, const uint8_t _index)
            {
                if (isCompositeChip(_device, _slot, _index))
                    return;
                const std::pair<RomSlot, uint8_t> slot{_slot, _index};
                if (std::find(result.begin(), result.end(), slot) == result.end())
                    result.push_back(slot);
            };
            for (const auto& spec : g_romFileSpecs)
            {
                if (spec.device == _device)
                    add(spec.slot, spec.index);
            }
            if (!result.empty())
                return result;
            for (const auto& entry : g_romRegistry)
            {
                if (usedBy(entry, _device))
                    add(entry.slot, entry.index);
            }
            return result;
        }


    } // namespace
    const FoundRom* RomInventory::find(const RomDevice _device, const RomSlot _slot, const uint8_t _index) const
    {
        // Prefer this model's explicit filename before its sibling board's aliases.
        for (const auto& rom : m_roms)
            if (rom.namedSpec && rom.namedSpec->device == _device && rom.slot() == _slot && rom.index() == _index)
                return &rom;

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

    // Joins a composite slot's chips into the whole image, or cuts the whole image back into
    // one chip, whichever direction the present files allow. Each direction reads only slots
    // that are physically present, so the two can never call each other in a circle: read()
    // delegates here exactly when find() came up empty.
    bool RomInventory::readComposite(std::vector<uint8_t>& _data, const RomDevice _device, const RomSlot _slot,
                                     const uint8_t _index) const
    {
        _data.clear();

        if (const auto* composite = findCompositeWhole(_device, _slot, _index))
        {
            std::vector<uint8_t> high;
            if (!find(_device, _slot, composite->firstChip) || !find(_device, _slot, composite->firstChip + 1) ||
                !read(_data, _device, _slot, composite->firstChip) ||
                !read(high, _device, _slot, composite->firstChip + 1) || _data.size() != composite->chipSize ||
                high.size() != composite->chipSize)
            {
                _data.clear();
                return false;
            }
            if (!composite->interleaved)
            {
                _data.insert(_data.end(), high.begin(), high.end());
                return true;
            }
            std::vector<uint8_t> whole(composite->wholeSize);
            for (size_t i = 0; i < composite->chipSize; ++i)
            {
                whole[i * 2] = _data[i];
                whole[i * 2 + 1] = high[i];
            }
            _data = std::move(whole);
            return true;
        }

        const auto* composite = findCompositeChip(_device, _slot, _index);
        if (!composite)
            return false;

        // A whole image of another size is a different generation of the slot - the MT-32's
        // banked 2.x firmware, say - and is not made of these chips.
        if (!find(_device, _slot, composite->wholeIndex) ||
            !read(_data, _device, _slot, composite->wholeIndex) || _data.size() != composite->wholeSize)
        {
            _data.clear();
            return false;
        }
        const uint8_t chip = _index - composite->firstChip;
        std::vector<uint8_t> part(composite->chipSize);
        if (composite->interleaved)
        {
            for (size_t i = 0; i < composite->chipSize; ++i)
                part[i] = _data[i * 2 + chip];
        }
        else
        {
            const auto first = _data.begin() + chip * composite->chipSize;
            part.assign(first, first + composite->chipSize);
        }
        _data = std::move(part);
        return true;
    }

    bool RomInventory::has(const RomDevice _device, const RomSlot _slot, const uint8_t _index) const
    {
        if (find(_device, _slot, _index))
            return true;
        // Both composite directions before the derived sources below: a board's own chips are
        // its data rather than a substitute for it.
        if (const auto* composite = findCompositeWhole(_device, _slot, _index))
        {
            if (find(_device, _slot, composite->firstChip) && find(_device, _slot, composite->firstChip + 1))
                return true;
        }
        if (const auto* composite = findCompositeChip(_device, _slot, _index))
        {
            const auto* whole = find(_device, _slot, composite->wholeIndex);
            if (whole && whole->size() == composite->wholeSize)
                return true;
        }
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
        // The board's own chips before the derived sources below. A failure here is not
        // final: the SC-88Pro's waves can still come from an SC-8820 or SC-8850 donor.
        if (!found && readComposite(_data, _device, _slot, _index))
            return true;
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
        if (_device == RomDevice::Cm64)
            return isComplete(RomDevice::Cm32l) && isComplete(RomDevice::Cm32p);

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

        if (_device == RomDevice::Cm64)
        {
            for (const auto half : g_cm64Halves)
            {
                const auto rows = missing(half);
                result.insert(result.end(), rows.begin(), rows.end());
            }
            return result;
        }

        for (const auto& entry : g_romRegistry)
        {
            if (!usedBy(entry, _device) || has(_device, entry.slot, entry.index) ||
                isCompositeChip(_device, entry.slot, entry.index))
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
        if (_device == RomDevice::Cm64)
        {
            for (const auto half : g_cm64Halves)
            {
                const auto specs = missingFiles(half);
                result.insert(result.end(), specs.begin(), specs.end());
            }
            return result;
        }
        for (const auto& spec : g_romFileSpecs)
        {
            if (spec.device != _device || has(_device, spec.slot, spec.index) ||
                isCompositeChip(_device, spec.slot, spec.index))
                continue;
            result.push_back(&spec);
        }
        return result;
    }

    std::string RomInventory::warnings(const RomDevice device, bool includeCustom) const
    {
        std::ostringstream result;
        if (device == RomDevice::Cm64)
        {
            for (const auto half : g_cm64Halves)
                result << warnings(half, includeCustom);
            return result.str();
        }
        for (const auto& rom : m_roms)
        {
            if (!rom.usedBy(device) || find(device, rom.slot(), rom.index()) != &rom)
                continue;
            if (rom.entry && rom.entry->badDump)
                result << rom.path << ": " << rom.entry->version << ". This is not a good dump of the original "
                       << "chip.\n";
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
        if (_device == RomDevice::Cm64)
        {
            text << "The CM-64 is a CM-32L and a CM-32P in one case and needs both boards' images.\n\n";
            for (const auto half : g_cm64Halves)
                text << "== " << toString(half) << " ==\n" << describeRequirements(half);
            return text.str();
        }
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
            // Number a slot only where this board fills it from more than one image.
            if (std::count_if(slots.begin(), slots.end(), [s = slot](const auto& _other)
                              { return _other.first == s; }) > 1)
                text << ' ' << (index + 1);
            text << " — "
                 << (found                           ? (found->entry ? "Found" : "Found (unverified)")
                         : has(_device, slot, index) ? "Available from a compatible image"
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
                    // Whichever digest identifies the row; a revision catalogued from a
                    // published reference has no MD5 to show.
                    text << (entry.hash.isValid() ? "MD5: " + entry.hash.toString()
                                                  : "SHA-1: " + entry.sha1.toString())
                         << " — " << entry.version << '\n';
                    known = true;
                }
            if (!known)
                text << "No reference hash is registered for this component. Filename and size are required.\n";
            text << '\n';
        }

        if (_device == RomDevice::Cm32l)
            text << "Wave alternatives: Wave ROM 1 is the complete 1 MiB PCM image, the file munt reads as "
                    "cm32l_pcm.rom. Wave ROM 2 and 3 are the two 512 KiB mask-ROM dumps it is made of, "
                    "R15449121 and R15179945 - together they supply the same image. Provide either form, "
                    "not both.\n";
        if (_device == RomDevice::Mt32)
            text << "Control alternatives: Control ROM 1 is the complete firmware - 64 KiB on a 1.x board, "
                    "128 KiB on a 2.x one. Control ROM 2 and 3 are the two 32 KiB EPROMs a 1.x board carries "
                    "instead, IC27 and IC26; they are byte-multiplexed rather than concatenated, and together "
                    "they supply the 64 KiB image. Provide either form, not both. The 2.x firmware has no "
                    "two-chip form.\n"
                    "Wave alternatives: Wave ROM 1 is R15449121, the late board's single PCM mask ROM and the "
                    "lower half of the CM-32L's image. Wave ROM 2 and 3 are R15179844 and R15179845, the same "
                    "data as the early board split it.\n"
                    "Reverb alternatives: R15179857 is the early board's microcode and R15179917 the one the "
                    "late board and the MT-100 carry in the same position. Either runs.\n";
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
            text << "The SC-88Pro board carries the same PCM on five 4 MiB mask ROMs instead, one per XP "
                        "chip select, and those are accepted too: CS0 and CS1 supply Wave ROM 1, CS2 and CS3 "
                        "supply Wave ROM 2, and CS4 is Wave ROM 3 itself. The four listed above are derived "
                        "references, hashed by cutting the VE-GSPro images at those boundaries rather than "
                        "taken from a dump of the board's own parts.\n";
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

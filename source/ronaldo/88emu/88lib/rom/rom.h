#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "88lib/boards/sc88types.h"
#include "88lib/deviceModel.h"
#include "88lib/rom/romRegistry.h"

#include "baseLib/md5.h"

namespace emu88Lib
{
    struct Cm32pRomSet
    {
        static constexpr size_t ProgramSize = 0x10000;
        static constexpr size_t WaveSize = 0x80000;
        using WaveRoms = std::array<std::vector<uint8_t>, 3>;
        std::vector<uint8_t> program;
        WaveRoms waves;
        bool isValid() const
        {
            if (program.size() != ProgramSize)
                return false;
            for (const auto& wave : waves)
                if (wave.size() != WaveSize)
                    return false;
            return true;
        }
    };

    // CM-32L: the 8095's control ROM, the LA32's PCM mask ROM and the Boss
    // reverb gate array's microcode ROM. The wave image is the two 512 KiB mask
    // ROMs concatenated in address order, R15449121 followed by R15179945 -
    // the same file munt reads as CM32L_PCM.ROM.
    struct Cm32lRomSet
    {
        static constexpr size_t ControlSize = 0x10000;
        static constexpr size_t WaveSize = 0x100000;
        static constexpr size_t ReverbSize = 0x8000;

        std::vector<uint8_t> control;
        std::vector<uint8_t> wave;
        std::vector<uint8_t> reverb;

        bool isValid() const
        {
            return control.size() == ControlSize && wave.size() == WaveSize && reverb.size() == ReverbSize;
        }
    };

    // Mask-ROM dumps of 16-bit parts sometimes store the two bytes of each CPU
    // word swapped, and both orientations of the same image circulate. Brings
    // one into CPU byte order if it is swapped, detected by scoring the H8/500
    // exception table in both orientations. Safe to call on an image that is
    // already correct, and on one that is neither.
    void normalizeH8WordOrder(std::vector<uint8_t>& _data);

    struct Sc88ProRomSet
    {
        static constexpr size_t FirmwareSize = 0x100000;
        static constexpr size_t WaveASize = 0x800000;
        static constexpr size_t WaveBSize = 0x800000;
        static constexpr size_t WaveCSize = 0x400000;
        static constexpr size_t WaveSize = WaveASize + WaveBSize + WaveCSize;

        std::vector<uint8_t> firmware;
        std::vector<uint8_t> waveA;
        std::vector<uint8_t> waveB;
        std::vector<uint8_t> waveC;
        std::string firmwareName;

        bool isValid() const
        {
            return firmware.size() == FirmwareSize && waveA.size() == WaveASize && waveB.size() == WaveBSize &&
                waveC.size() == WaveCSize;
        }
    };

    struct Sc8850RomSet
    {
        static constexpr size_t CpuSize = 0x10000;
        static constexpr size_t ProgramSize = 0x100000;
        static constexpr size_t DataSize = 0x200000;

        std::vector<uint8_t> cpu;
        std::vector<uint8_t> program;
        std::vector<uint8_t> data;

        bool isValid() const
        {
            return cpu.size() == CpuSize && program.size() == ProgramSize && data.size() == DataSize;
        }
    };

    // SC-8820 waves can supply the exact SC-88Pro set. For SC-8850 they were
    // only a bring-up source; that model requires its native pair below.
    struct Sc8820WaveRomSet
    {
        static constexpr size_t Cs0Size = 0x1000000;
        static constexpr size_t Cs1Size = 0x0800000;
    };

    // Native SC-8850 PCM dump. IC53 and IC54 are two 128-Mbit, 16-bit mask
    // ROMs. The combined dump is stored in physical-ROM order: IC53 first,
    // followed by IC54. Both XP chips share this complete bus.
    struct Sc8850WaveRomSet
    {
        static constexpr size_t ChipSize = 0x1000000;
        static constexpr size_t Size = ChipSize * 2;

        std::vector<uint8_t> romA;
        std::vector<uint8_t> romB;

        bool isValid() const { return romA.size() == ChipSize && romB.size() == ChipSize; }
    };

    // SC-55 family (sc55mk2.h): the H8/532's 32 KiB on-chip boot ROM, the
    // program ROM and the raw PCM mask-ROM dumps.
    struct Sc55RomSet
    {
        static constexpr size_t InternalRomSize = 0x8000;

        DeviceModel model = DeviceModel::Sc55Mk2;
        std::vector<uint8_t> internalRom;
        std::vector<uint8_t> programRom;
        std::array<std::vector<uint8_t>, 3> waveRom;

        Sc55DeviceProfile profile() const { return getSc55DeviceProfile(model); }
        bool isFirstGen() const { return profile().generation == Sc55Generation::First; }

        bool isValid() const
        {
            if (!isSc55Model(model) || internalRom.size() != InternalRomSize)
                return false;
            const auto p = profile();
            if (programRom.size() != p.programSize)
                return false;
            for (size_t i = 0; i < waveRom.size(); ++i)
                if (waveRom[i].size() != p.waveSizes[i])
                    return false;
            return true;
        }
    };

    // Normalize word-swapped firmware into CPU byte order. Registry-loaded
    // images already carry their board identity and orientation.
    class Rom
    {
    public:
        static constexpr size_t Size = 0x80000; // 512 KiB

        Rom() = default;
        explicit Rom(const std::string& _filename);
        Rom(std::vector<uint8_t> _data, std::string _name);
        // A correctly named custom dump that has no catalogued hash. The board
        // model is supplied by the filename contract rather than ROM identity.
        Rom(std::vector<uint8_t> _data, std::string _name, Model _assumedModel);
        // Identified by content: _entry is the registry row this image matched,
        // and _data is already normalized into CPU byte order.
        Rom(std::vector<uint8_t> _data, const RomRegistryEntry* _entry);

        std::vector<uint8_t> takeData() { return std::move(m_data); }
        const std::string& getName() const { return m_name; }
        const baseLib::MD5& getHash() const { return m_hash; }

        bool isValid() const { return m_data.size() == Size; }

        // The registry row this image was identified as, or null for a dump
        // loaded from an explicit path that matches nothing catalogued.
        const RomRegistryEntry* registryEntry() const { return m_entry; }

        // Whether the image resolved to a catalogued revision at all.

        // Which board this image belongs to. Known dumps use their content;
        // custom dumps use the conventional filename selected by the loader.
        // The SC-88 and SC-88VL differ in one port meaning (P6DR is the LCD
        // power line on the VL only), so the distinction must be retained.
        bool modelIsKnown() const { return m_entry != nullptr; }

        // Direct-path, unrecognized dumps default to Sc88; filename-loaded custom
        // dumps carry their requested board in m_assumedModel.
        Model model() const;

    private:
        void normalize();
        void identify();

        std::string m_name;
        std::vector<uint8_t> m_data;
        baseLib::MD5 m_hash;
        const RomRegistryEntry* m_entry = nullptr;
        Model m_assumedModel = Model::Sc88;
    };

    // Wave ROM: the four 2 MiB PCM mask ROMs (Roland "PCM_IC_325..328") the XP
    // reads its sample data from. The XP addresses them as four 2 MiB banks
    // selected by bits 20..21 of its 22-bit wave address, so we keep the
    // de-scrambled result as one flat 8 MiB image and let the XP index it
    // linearly.
    //
    // Raw dumps are in physical pin order and must be de-scrambled; the SC-88
    // uses rLib::rom::Pcm16.
    class WaveRom
    {
    public:
        static constexpr size_t ChipSize = 0x200000; // 2 MiB per chip
        static constexpr size_t ChipCount = 4; // IC325..IC328
        static constexpr size_t Size = ChipSize * ChipCount; // 8 MiB logical

        WaveRom() = default;
        // _chips[b] = raw physical-pin-order dump of bank b (2 MiB each).
        explicit WaveRom(const std::array<std::vector<uint8_t>, ChipCount>& _chips);

        // De-scrambled 8 MiB logical PCM, ready for the XP.
        std::vector<uint8_t> takeData() { return std::move(m_data); }

        bool isValid() const { return m_data.size() >= Size; }

        // SC-88 de-scramble of one 1 MiB-aligned region. The address and data
        // line permutations are rLib::rom::Pcm16 in common/romDescramble.h,
        // also used by compatible PCM wave dumps.
        static void unscramble(const uint8_t* _raw, size_t _rawLen, uint8_t* _dst, size_t _dstCap);
        static std::vector<uint8_t> decodeXpWaveDump(const std::vector<uint8_t>& raw);

    private:
        std::vector<uint8_t> m_data;
    };
} // namespace emu88Lib

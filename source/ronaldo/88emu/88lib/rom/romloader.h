#pragma once

#include <string>
#include <vector>

#include "88lib/boards/sc88types.h"
#include "88lib/deviceModel.h"
#include "88lib/rom/rom.h"
#include "88lib/rom/romRegistry.h"

#include "synthLib/romLoader.h"

namespace emu88Lib
{
    // One image found on the search paths, either resolved by a known hash or by
    // a board's standardized filename and exact size. The data itself is not kept - a full SC-8850 wave
    // dump is 32 MiB and there is no reason to hold every board's set in memory
    // at once - so an inventory is cheap to build and to keep.
    struct FoundRom
    {
        const RomRegistryEntry* entry = nullptr;
        const RomFileSpec* namedSpec = nullptr;
        std::string path;
        // The dump on disk is byte-swapped relative to the registry's canonical
        // form and has to be normalized after reading.
        bool needsWordSwap = false;
        // Embedded images are read as bounded regions of a data container.
        size_t fileOffset = 0;
        bool embedded = false;
        // True only for a named file whose slot has known reference hashes and
        // whose content matches none of them. It is still deliberately usable.
        bool hashMismatch = false;
        baseLib::MD5 actualHash;

        bool isValid() const { return entry != nullptr || namedSpec != nullptr; }
        RomSlot slot() const { return namedSpec ? namedSpec->slot : entry->slot; }
        uint8_t index() const { return namedSpec ? namedSpec->index : entry->index; }
        size_t size() const { return namedSpec ? namedSpec->size : entry->size; }
        bool usedBy(RomDevice _device) const
        {
            if (!namedSpec)
                return emu88Lib::usedBy(*entry, _device);
            // Both Pro boards use the same PCM, including custom named chip dumps.
            const auto isPro = [](RomDevice d) { return d == RomDevice::Sc88Pro || d == RomDevice::VeGsPro; };
            return namedSpec->device == _device ||
                (namedSpec->slot == RomSlot::Wave && isPro(namedSpec->device) && isPro(_device));
        }
    };

    // What the search paths hold right now, identified by filename first and
    // then by content.
    //
    // Built by RomLoader::scan(); known hashes resolve independently of filenames.
    class RomInventory
    {
    public:
        const FoundRom* find(RomDevice _device, RomSlot _slot, uint8_t _index = 0) const;

        // Includes SC-88Pro wave chips derivable from recognized SC-8820/8850
        // images. find() itself only reports files physically present.
        bool has(RomDevice _device, RomSlot _slot, uint8_t _index = 0) const;

        // Reads and normalizes one image. Returns false when the slot is empty
        // or the file has changed underneath us since the scan.
        // Derived Pro waves are returned in native scrambled chip order, with
        // their contents checked against the native Pro registry hashes.
        bool read(std::vector<uint8_t>& _data, RomDevice _device, RomSlot _slot, uint8_t _index = 0) const;

        // True when every required image was found or can be derived.
        // This is what makes a device selectable: a board with a missing wave
        // ROM is not offered rather than booted into silence.
        bool isComplete(RomDevice _device) const;

        // The registry rows this board needs and does not have, in table order.
        std::vector<const RomRegistryEntry*> missing(RomDevice _device) const;
        std::vector<const RomFileSpec*> missingFiles(RomDevice _device) const;
        std::string warnings(RomDevice device, bool includeCustom = true) const;

        std::string describeRequirements(RomDevice _device) const;

        void add(FoundRom _rom) { m_roms.push_back(std::move(_rom)); }
        const std::vector<FoundRom>& roms() const { return m_roms; }

    private:
        const FoundRom* findSc88ProWaveSource(uint8_t _index, size_t& _offset) const;
        bool readComposite(std::vector<uint8_t>& _data, RomDevice _device, RomSlot _slot, uint8_t _index) const;
        std::vector<FoundRom> m_roms;
    };

    // Locates SC-55/SC-88 series ROM images on the standard synthLib::RomLoader
    // search paths. Standardized filenames take priority and permit custom ROMs;
    // known hashes remain the filename-independent fallback.
    //
    // Raw images are selected by size; oversized Pro EPROM dumps are checked for
    // valid 1 MiB copies, and DLL/dylib containers are searched for embedded wave
    // headers. Subfolders are searched too.
    class RomLoader : synthLib::RomLoader
    {
    public:
        // What the search paths currently hold. The sweep result is cached,
        // because creating a device asks for one component at a time and the
        // ROM folder does not change underneath a running session on its own.
        static RomInventory scan();

        // Sweeps the search paths again and replaces the cached result. Call it
        // where the user may just have added a dump - selecting a device, or
        // restarting the current one. MD5s are remembered per file for the
        // lifetime of the process, keyed by path, size and modification time,
        // so a rescan costs a directory walk rather than re-reading every dump.
        static RomInventory rescan();

        // Searches a file as data, without loading executable code. ROM header
        // candidates must match complete SC-8820 chip hashes before acceptance.
        static std::vector<FoundRom> findEmbeddedWaveRoms(const std::string& _path);

        // Whether every image the registry lists for a board was found, which is
        // what makes it selectable.
        static bool isDeviceAvailable(RomDevice _device);
        static bool isDeviceAvailable(DeviceModel _model);

        // Maps a DeviceModel onto the registry's wider board list.
        static RomDevice toRomDevice(DeviceModel _model);

        // The SC-88 or SC-88VL control ROM. Unlike the previous filename-based
        // loader this never substitutes the other variant: the two firmwares
        // differ in one port meaning (P6DR is the LCD power line on the VL
        // only) and booting the wrong one blanks the display.
        static Rom findROM(Model _model = Model::Sc88VL);

        // The SC-88Pro board's set. The VE-GS Pro is the same board with its own
        // control ROM and the same waves, so _device picks whose control ROM is read.
        static Sc88ProRomSet findSc88ProRomSet(RomDevice _device = RomDevice::Sc88Pro);

        // SC-8850 uses the SH7016's 64 KiB internal ROM, a 1 MiB CS0
        // executable flash and a 2 MiB CS3 data/tone flash.
        static Sc8850RomSet findSc8850RomSet();
        static Sc8850WaveRomSet findSc8850WaveRomSet();

        // SC-55 family: the H8/532 on-chip ROM, program ROM and every PCM mask
        // ROM required by the selected board revision.
        static Sc55RomSet findSc55RomSet(DeviceModel _model);
        static Cm32pRomSet findCm32pRomSet();

        // CM-32L: control ROM, the LA32's PCM image and the Boss reverb microcode.
        static Cm32lRomSet findCm32lRomSet();

        // The four internal PCM chips, de-scrambled into the XP's flat 8 MiB
        // space.
        static WaveRom findWaveRom();
        static std::vector<uint8_t> findXpgsWaveRom();
    };
} // namespace emu88Lib

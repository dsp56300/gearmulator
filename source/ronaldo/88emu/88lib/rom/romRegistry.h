#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "baseLib/md5.h"

namespace emu88Lib
{
    // ---------------------------------------------------------------------
    // The registry of known SC-55/SC-88 series ROM images
    // ---------------------------------------------------------------------
    //
    // Known dumps are content-addressed, so they can be renamed and still resolve
    // to the right slot. The filename table below additionally permits custom
    // ROM builds when they use the board's standardized basename and exact size.
    //
    // Adding support for a newly dumped firmware revision means adding a row
    // here. Nothing else has to change: the scanner derives the candidate sizes
    // to look at, and each board derives the set of images it needs, from this
    // table alone.

    // Includes SC-8820 wave sources used to derive the SC-88Pro set.
    enum class RomDevice : uint8_t
    {
        Sc88,
        Sc88VL,
        Sc88Pro,
        Sc8850,
        Sc55Mk2,
        Sc8820,
        Sc55Mk1,
        Sc55St,
        Cm300,
        Scb55,
        Rlp3237,
        Sc155,
        Sc155Mk2,
        Xpgs,
        Cm32p,
        VeGsPro,
        Scc1a,
        // Scaffolding: no registry row names the CM-64 yet, which is what keeps
        // RomInventory::isComplete() - and so the board - reporting unavailable.
        Cm64,

        Count
    };

    // Shared images name all accepting boards in one registry entry.
    using RomDevices = uint32_t;
    static_assert(static_cast<unsigned>(RomDevice::Count) <= sizeof(RomDevices) * 8);

    constexpr RomDevices romDeviceBit(const RomDevice _device)
    {
        return static_cast<RomDevices>(1u << static_cast<uint8_t>(_device));
    }

    template <typename... Ts> constexpr RomDevices romDevices(const Ts... _devices)
    {
        return (romDeviceBit(_devices) | ...);
    }

    // Which slot of a board's ROM set an image fills.
    enum class RomSlot : uint8_t
    {
        // The single H8 control ROM of an SC-88 class board: code and data in
        // one mask ROM.
        Control,
        // On-chip boot ROM, kept separate from the external program ROM
        // (SC-55mk2 H8/532, SC-8850 SH7016).
        Internal,
        // External executable flash.
        Program,
        // Non-executable data/tone flash.
        Data,
        // PCM sample data.
        Wave,

        Count
    };

    // Required filenames are intentionally independent of the known hashes
    // below. A correctly sized file with one of these basenames is allowed to
    // boot even when its contents are custom; a known hash is only required for
    // the filename-independent fallback.
    struct RomFileSpec
    {
        RomDevice device;
        RomSlot slot;
        uint8_t index;
        size_t size;
        const char* filename;
        bool normalizeH8Words;
    };

    inline constexpr RomFileSpec g_romFileSpecs[] = {
        {RomDevice::Cm32p, RomSlot::Program, 0, 0x10000, "cm32p_program.bin", false},
        {RomDevice::Cm32p, RomSlot::Wave, 0, 0x80000, "cm32p_wave0.bin", false},
        {RomDevice::Cm32p, RomSlot::Wave, 1, 0x80000, "cm32p_wave1.bin", false},
        {RomDevice::Cm32p, RomSlot::Wave, 2, 0x80000, "cm32p_wave2.bin", false},
        {RomDevice::Sc8820, RomSlot::Internal, 0, 0x10000, "sc8820_internal.bin", false},
        {RomDevice::Sc8820, RomSlot::Internal, 0, 0x20000, "sc8820_internal.bin", false},
        {RomDevice::Sc8820, RomSlot::Program, 0, 0x200000, "sc8820_program.bin", false},
        {RomDevice::Sc8820, RomSlot::Wave, 0, 0x1000000, "sc8820_wave0.bin", false},
        {RomDevice::Sc8820, RomSlot::Wave, 1, 0x800000, "sc8820_wave1.bin", false},
        {RomDevice::Xpgs, RomSlot::Control, 0, 0x80000, "xpgs_control.bin", true},
        {RomDevice::Xpgs, RomSlot::Wave, 0, 0x200000, "xpgs_wave0.bin", false},
        {RomDevice::Xpgs, RomSlot::Wave, 1, 0x200000, "xpgs_wave1.bin", false},
        {RomDevice::Xpgs, RomSlot::Wave, 2, 0x200000, "xpgs_wave2.bin", false},
        {RomDevice::Xpgs, RomSlot::Wave, 3, 0x200000, "xpgs_wave3.bin", false},
        {RomDevice::Xpgs, RomSlot::Wave, 4, 0x200000, "xpgs_wave4.bin", false},
        {RomDevice::Sc88, RomSlot::Control, 0, 0x80000, "sc88_control.bin", true},
        {RomDevice::Sc88VL, RomSlot::Control, 0, 0x80000, "sc88vl_control.bin", true},
        {RomDevice::Sc88Pro, RomSlot::Control, 0, 0x100000, "sc88pro_control.bin", true},
        {RomDevice::Sc88Pro, RomSlot::Wave, 0, 0x800000, "sc88pro_wave0.bin", false},
        {RomDevice::Sc88Pro, RomSlot::Wave, 1, 0x800000, "sc88pro_wave1.bin", false},
        {RomDevice::Sc88Pro, RomSlot::Wave, 2, 0x400000, "sc88pro_wave2.bin", false},
        {RomDevice::VeGsPro, RomSlot::Control, 0, 0x100000, "vegspro_control.bin", true},
        {RomDevice::VeGsPro, RomSlot::Wave, 0, 0x800000, "vegspro_wave0.bin", false},
        {RomDevice::VeGsPro, RomSlot::Wave, 1, 0x800000, "vegspro_wave1.bin", false},
        {RomDevice::VeGsPro, RomSlot::Wave, 2, 0x400000, "vegspro_wave2.bin", false},
        {RomDevice::Sc8850, RomSlot::Internal, 0, 0x10000, "sc8850_internal.bin", false},
        {RomDevice::Sc8850, RomSlot::Program, 0, 0x100000, "sc8850_program.bin", false},
        {RomDevice::Sc8850, RomSlot::Data, 0, 0x200000, "sc8850_data.bin", false},
        {RomDevice::Sc8850, RomSlot::Wave, 0, 0x2000000, "sc8850_wave.bin", false},
        {RomDevice::Sc8850, RomSlot::Wave, 0, 0x1000000, "sc8850_wave0.bin", false},
        {RomDevice::Sc8850, RomSlot::Wave, 1, 0x1000000, "sc8850_wave1.bin", false},
        {RomDevice::Sc55Mk2, RomSlot::Internal, 0, 0x8000, "sc55mk2_internal.bin", false},
        {RomDevice::Sc55Mk2, RomSlot::Program, 0, 0x80000, "sc55mk2_program.bin", false},
        {RomDevice::Sc55Mk2, RomSlot::Wave, 0, 0x200000, "sc55mk2_wave0.bin", false},
        {RomDevice::Sc55Mk2, RomSlot::Wave, 1, 0x100000, "sc55mk2_wave1.bin", false},
        {RomDevice::Sc55Mk1, RomSlot::Internal, 0, 0x8000, "sc55mk1_internal.bin", false},
        {RomDevice::Sc55Mk1, RomSlot::Program, 0, 0x40000, "sc55mk1_program.bin", false},
        {RomDevice::Sc55Mk1, RomSlot::Wave, 0, 0x100000, "sc55mk1_wave0.bin", false},
        {RomDevice::Sc55Mk1, RomSlot::Wave, 1, 0x100000, "sc55mk1_wave1.bin", false},
        {RomDevice::Sc55Mk1, RomSlot::Wave, 2, 0x100000, "sc55mk1_wave2.bin", false},
        {RomDevice::Sc55St, RomSlot::Internal, 0, 0x8000, "sc55st_internal.bin", false},
        {RomDevice::Sc55St, RomSlot::Program, 0, 0x80000, "sc55st_program.bin", false},
        {RomDevice::Sc55St, RomSlot::Wave, 0, 0x200000, "sc55st_wave0.bin", false},
        {RomDevice::Sc55St, RomSlot::Wave, 1, 0x100000, "sc55st_wave1.bin", false},
        {RomDevice::Cm300, RomSlot::Internal, 0, 0x8000, "cm300_internal.bin", false},
        {RomDevice::Cm300, RomSlot::Program, 0, 0x40000, "cm300_program.bin", false},
        {RomDevice::Cm300, RomSlot::Wave, 0, 0x100000, "cm300_wave0.bin", false},
        {RomDevice::Cm300, RomSlot::Wave, 1, 0x100000, "cm300_wave1.bin", false},
        {RomDevice::Cm300, RomSlot::Wave, 2, 0x100000, "cm300_wave2.bin", false},
        {RomDevice::Scc1a, RomSlot::Internal, 0, 0x8000, "scc1a_internal.bin", false},
        {RomDevice::Scc1a, RomSlot::Program, 0, 0x40000, "scc1a_program.bin", false},
        {RomDevice::Scc1a, RomSlot::Wave, 0, 0x100000, "scc1a_wave0.bin", false},
        {RomDevice::Scc1a, RomSlot::Wave, 1, 0x100000, "scc1a_wave1.bin", false},
        {RomDevice::Scc1a, RomSlot::Wave, 2, 0x100000, "scc1a_wave2.bin", false},
        {RomDevice::Scb55, RomSlot::Internal, 0, 0x8000, "scb55_internal.bin", false},
        {RomDevice::Scb55, RomSlot::Program, 0, 0x40000, "scb55_program.bin", false},
        {RomDevice::Scb55, RomSlot::Wave, 0, 0x200000, "scb55_wave0.bin", false},
        {RomDevice::Scb55, RomSlot::Wave, 1, 0x100000, "scb55_wave1.bin", false},
        {RomDevice::Rlp3237, RomSlot::Internal, 0, 0x8000, "rlp3237_internal.bin", false},
        {RomDevice::Rlp3237, RomSlot::Program, 0, 0x40000, "rlp3237_program.bin", false},
        {RomDevice::Rlp3237, RomSlot::Wave, 0, 0x200000, "rlp3237_wave0.bin", false},
        {RomDevice::Sc155, RomSlot::Internal, 0, 0x8000, "sc155_internal.bin", false},
        {RomDevice::Sc155, RomSlot::Program, 0, 0x40000, "sc155_program.bin", false},
        {RomDevice::Sc155, RomSlot::Wave, 0, 0x100000, "sc155_wave0.bin", false},
        {RomDevice::Sc155, RomSlot::Wave, 1, 0x100000, "sc155_wave1.bin", false},
        {RomDevice::Sc155, RomSlot::Wave, 2, 0x100000, "sc155_wave2.bin", false},
        {RomDevice::Sc155Mk2, RomSlot::Internal, 0, 0x8000, "sc155mk2_internal.bin", false},
        {RomDevice::Sc155Mk2, RomSlot::Program, 0, 0x80000, "sc155mk2_program.bin", false},
        {RomDevice::Sc155Mk2, RomSlot::Wave, 0, 0x200000, "sc155mk2_wave0.bin", false},
        {RomDevice::Sc155Mk2, RomSlot::Wave, 1, 0x100000, "sc155mk2_wave1.bin", false},
        {RomDevice::Sc88, RomSlot::Wave, 0, 0x200000, "sc88_wave0.bin", false},
        {RomDevice::Sc88, RomSlot::Wave, 1, 0x200000, "sc88_wave1.bin", false},
        {RomDevice::Sc88, RomSlot::Wave, 2, 0x200000, "sc88_wave2.bin", false},
        {RomDevice::Sc88, RomSlot::Wave, 3, 0x200000, "sc88_wave3.bin", false},
        {RomDevice::Sc88VL, RomSlot::Wave, 0, 0x200000, "sc88vl_wave0.bin", false},
        {RomDevice::Sc88VL, RomSlot::Wave, 1, 0x200000, "sc88vl_wave1.bin", false},
        {RomDevice::Sc88VL, RomSlot::Wave, 2, 0x200000, "sc88vl_wave2.bin", false},
        {RomDevice::Sc88VL, RomSlot::Wave, 3, 0x200000, "sc88vl_wave3.bin", false},
    };

    inline constexpr size_t g_romFileSpecCount = sizeof(g_romFileSpecs) / sizeof(g_romFileSpecs[0]);

    struct RomRegistryEntry
    {
        // Which boards accept this image in this slot.
        RomDevices devices;
        RomSlot slot;
        // Chip index within a multi-image slot, or zero for a single image.
        uint8_t index;
        // Exact byte count. Nothing of another size is ever read, so this
        // doubles as the scan's prefilter.
        size_t size;
        // MD5 of this source layout, after optional CPU word-order normalization.
        baseLib::MD5 hash;
        // How this revision identifies itself. Firmware carries a version
        // string or a pair of version bytes; a mask ROM is identified by its
        // Roland part number or board position instead. Shown in the UI, so it
        // has to be meaningful on its own.
        const char* version;
        // Mask-ROM dumps of 16-bit parts sometimes store the two bytes of each
        // CPU word swapped, and both orientations of the same ROM circulate.
        // Where this is set the scanner also tries the swapped form of a
        // candidate and normalizes a match into CPU order, so either resolves
        // to this entry and getData() is always ready for the H8 core.
        bool wordSwapped;
        bool badDump = false;
        // Linear XP dumps require address/data conversion before board loading.
        bool xpWaveDump = false;
    };

    // The SC-88 and SC-88VL run different firmware on the same board, and the
    // difference is not cosmetic: P6DR is the LCD power line on the VL only, so
    // booting one image as the other blanks the display. Content addressing is
    // what keeps them apart.
    inline constexpr RomRegistryEntry g_romRegistry[] = {
        {romDevices(RomDevice::Cm32p), RomSlot::Program, 0, 0x10000, baseLib::MD5("ccd61a220f433a62a53b48eb9bb36827"),
         "1.00", false},
        {romDevices(RomDevice::Cm32p), RomSlot::Wave, 0, 0x80000, baseLib::MD5("21efdc888020fe99303ef34265349021"),
         "R15179970", false},
        {romDevices(RomDevice::Cm32p), RomSlot::Wave, 1, 0x80000, baseLib::MD5("69e39536391fe79250f67cd3c7794217"),
         "R15179971", false},
        {romDevices(RomDevice::Cm32p), RomSlot::Wave, 2, 0x80000, baseLib::MD5("512bee180e2390b896484adb01f1d215"),
         "R15179972", false},

        // -----------------------------------------------------------------
        // SC-88 / SC-88VL
        // -----------------------------------------------------------------
        // Both state their revision as two bytes following the model name near
        // the end of the image ("SC-88 Ver101" / 01 01, and 01 04 for the VL).
        // The VL dump in circulation is byte-swapped, the SC-88's is not.
        {romDevices(RomDevice::Sc88), RomSlot::Control, 0, 0x80000, baseLib::MD5("0ac771782ea58a53af590ebdf140d517"),
         "1.01", true},
        {romDevices(RomDevice::Sc88VL), RomSlot::Control, 0, 0x80000, baseLib::MD5("25e016e93c8a44ba3c35584462b56d72"),
         "1.04", true},

        // CPU-order hash; the word-swapped image has MD5 c9978604e8036a22a5f7381ae22aa3db.
        {romDevices(RomDevice::Xpgs), RomSlot::Control, 0, 0x80000, baseLib::MD5("2e6d39474b66c7a6a48f7a1a3e5dc403"),
         "G-800 GS-64 3.00 RES-1", true},
        {romDevices(RomDevice::Xpgs), RomSlot::Wave, 4, 0x200000, baseLib::MD5("b2c30824c92c214cb7314d2a58409423"),
         "XP-GS E-10", false},

        // The four internal PCM chips, in bank order.
        {romDevices(RomDevice::Sc88, RomDevice::Sc88VL, RomDevice::Xpgs), RomSlot::Wave, 0, 0x200000,
         baseLib::MD5("6a92b7de3ac7b8205d29ec4497644beb"), "IC325", false},
        {romDevices(RomDevice::Sc88, RomDevice::Sc88VL, RomDevice::Xpgs), RomSlot::Wave, 1, 0x200000,
         baseLib::MD5("d98f4b255d3a7dc830d92c71c25ce2eb"), "IC326", false},
        {romDevices(RomDevice::Sc88, RomDevice::Sc88VL, RomDevice::Xpgs), RomSlot::Wave, 2, 0x200000,
         baseLib::MD5("c05b103d4db110b3962431173cc72967"), "IC327", false},
        {romDevices(RomDevice::Sc88, RomDevice::Sc88VL, RomDevice::Xpgs), RomSlot::Wave, 3, 0x200000,
         baseLib::MD5("bccc26c34cac0d5509e8efb645043b5b"), "IC328", false},

        // -----------------------------------------------------------------
        // SC-88Pro
        // -----------------------------------------------------------------
        // Two control ROMs run on this board and are listed in preference
        // order: the SC-88Pro's own firmware, which states "Ver1.02" in its
        // service screen data, then an SC-GS board dump. That one identifies
        // as "SC-GS" rather than SC-88Pro and carries no version string, so it
        // is named by the revision letter and year in the production-board
        // record that follows the board name. It keeps the SC-88Pro's panel and
        // LCD code and takes MIDI through the sub-MCU.
        //
        // Its three wave ROMs are the VE-GSPro sample set, by part number,
        // shared with the VE-GS Pro below.
        {romDevices(RomDevice::Sc88Pro), RomSlot::Control, 0, 0x100000,
         baseLib::MD5("9d4c2f123b4451d8ee75c3b982760f28"), "1.02", true},
        {romDevices(RomDevice::Sc88Pro), RomSlot::Control, 0, 0x100000,
         baseLib::MD5("784b3ea762b5f96cabdceb33d121d5e4"), "SC-GS A '96", true},
        {romDevices(RomDevice::Sc88Pro, RomDevice::VeGsPro), RomSlot::Wave, 0, 0x800000,
         baseLib::MD5("bd33b20bb5e8f51e436e141f81a75c14"), "R01567167, XP-GS 1.01", false},
        {romDevices(RomDevice::Sc88Pro, RomDevice::VeGsPro), RomSlot::Wave, 1, 0x800000,
         baseLib::MD5("125ea2056dc208f04a141b3dcdffdb1b"), "R01567178, SC-GS 1.00", false},
        {romDevices(RomDevice::Sc88Pro, RomDevice::VeGsPro), RomSlot::Wave, 2, 0x400000,
         baseLib::MD5("48c3887c9a2a574b907242640fa0a320"), "R01233667, SC-GS 1.00", false},

        // -----------------------------------------------------------------
        // VE-GS Pro
        // -----------------------------------------------------------------
        // The headless expansion board: the SC-88Pro engine on the same board
        // map, without the panel sub-MCU. Its program ROM identifies as "SC-GS"
        // revision B '97, has no panel or LCD code and takes MIDI on the H8's
        // SCI. CPU-order hash; the circulating R01780078 dump is word-swapped
        // (MD5 24e1d55fc9e1b03c135994e5ea8589fa).
        {romDevices(RomDevice::VeGsPro), RomSlot::Control, 0, 0x100000,
         baseLib::MD5("f836d9491075c28c1c0587d4876cec65"), "R01780078, SC-GS B '97", true},

        // -----------------------------------------------------------------
        // SC-8850
        // -----------------------------------------------------------------
        // The SH7016's on-chip ROM, the CS0 executable flash and the CS3
        // data/tone flash. The wave dump is one image covering both 128-Mbit
        // mask ROMs (IC53 then IC54); the loader splits it.
        {romDevices(RomDevice::Sc8850), RomSlot::Internal, 0, 0x10000, baseLib::MD5("efe1ffb0ccbe1b2ec454692c522494fc"),
         "SH7016 boot", false},
        {romDevices(RomDevice::Sc8850), RomSlot::Program, 0, 0x100000, baseLib::MD5("554d5997dcd9ce6fa0777092ff48f6fa"),
         "XP-GS 1.01 / SC-GS 1.00", false},
        {romDevices(RomDevice::Sc8850), RomSlot::Data, 0, 0x200000, baseLib::MD5("06eee65647b66109efb01eabd6d71248"),
         "tone flash", false},
        {romDevices(RomDevice::Sc8850), RomSlot::Wave, 0, 0x2000000, baseLib::MD5("e8eb6ac0e394997dfbc17064f9f4a70e"),
         "IC53+IC54, unscrambled", false},

        {romDevices(RomDevice::Sc8850), RomSlot::Wave, 1, 0x1000000, baseLib::MD5("d5016082b2d0f12c235bacb9469acb07"),
         "IC54, decoded", false},
        {romDevices(RomDevice::Sc8850, RomDevice::Sc8820), RomSlot::Wave, 0, 0x1000000,
         baseLib::MD5("aeac599d472090323bf17a1905cfb166"), "R01891445, XP dump layout", false, false, true},
        // Reference hash derived by encoding the verified IC54 decoded image.
        {romDevices(RomDevice::Sc8850), RomSlot::Wave, 1, 0x1000000, baseLib::MD5("960e4c3d9195968a22567d36c763988f"),
         "R01891456, XP dump layout (derived reference)", false, false, true},

        // -----------------------------------------------------------------
        // SC-8820
        // -----------------------------------------------------------------
        // The first 20 MiB supplies the exact SC-88Pro waves. SC-8850 still
        // requires its native wave image above; the extensions differ.
        {romDevices(RomDevice::Sc8820), RomSlot::Internal, 0, 0x10000, baseLib::MD5("b03dc0c554fae77bb1782da4ccfadfbd"),
         "BAD_DUMP: reconstructed CPU ROM", false, true},
        {romDevices(RomDevice::Sc8820), RomSlot::Program, 0, 0x200000, baseLib::MD5("79f7d20691ef98b4408592e0e70132d5"),
         "program and tone flash, 2000-11-28", false},
        {romDevices(RomDevice::Sc8820, RomDevice::Sc8850), RomSlot::Wave, 0, 0x1000000,
         baseLib::MD5("2ce0dfb99b0fbe4313b225d37d68ac95"), "CS0", false},
        {romDevices(RomDevice::Sc8820), RomSlot::Wave, 1, 0x800000, baseLib::MD5("35551cfb0cb36b95de6301a617249178"),
         "CS1", false},

        // -----------------------------------------------------------------
        // SC-55mk2
        // -----------------------------------------------------------------
        // The H8/532's on-chip boot ROM, the program ROM and the two raw PCM
        // mask-ROM dumps the board de-scrambles itself. The program ROM states
        // the GS spec level ("GS-28 VER=2.00") rather than a firmware revision.
        {romDevices(RomDevice::Sc55Mk2, RomDevice::Sc55St, RomDevice::Sc155Mk2), RomSlot::Internal, 0, 0x8000,
         baseLib::MD5("4ca058f7db05f51e97bb30a162e9610a"), "H8/532 boot", false},
        {romDevices(RomDevice::Sc55Mk2, RomDevice::Sc155Mk2), RomSlot::Program, 0, 0x80000,
         baseLib::MD5("63b24c7193ce34afefce9cec32ac39f0"), "GS-28 2.00", false},
        {romDevices(RomDevice::Sc55Mk2), RomSlot::Program, 0, 0x80000, baseLib::MD5("34e5802afd4c12230e775e0c94c4873b"),
         "GS-28 2.00, SC-55 capital tone fallback / v1 drums", false},
        {romDevices(RomDevice::Sc55Mk2, RomDevice::Sc55St, RomDevice::Scb55, RomDevice::Sc155Mk2), RomSlot::Wave, 0,
         0x200000, baseLib::MD5("30df645acf7f1b621d5f2891ea53e00b"), "PCM 1", false},
        {romDevices(RomDevice::Sc55Mk2, RomDevice::Sc55St, RomDevice::Scb55, RomDevice::Sc155Mk2), RomSlot::Wave, 1,
         0x100000, baseLib::MD5("f86e0433a11707a048a8a79e8d98a3be"), "PCM 2", false},

        // -----------------------------------------------------------------
        // SC-55 (first generation), firmware 1.21
        // -----------------------------------------------------------------
        // Unlike the mkII, the original board has a 256 KiB program ROM and
        // three separate 1 MiB PCM mask ROMs.
        {romDevices(RomDevice::Sc55Mk1), RomSlot::Internal, 0, 0x8000, baseLib::MD5("462cb3a2ce9e54f4e54a52f643931ffd"),
         "1.21, R15199778", false},
        {romDevices(RomDevice::Sc55Mk1), RomSlot::Program, 0, 0x40000, baseLib::MD5("6b61186953b50d900e430ae6a996bda7"),
         "1.21, R15209363", false},
        {romDevices(RomDevice::Sc55Mk1, RomDevice::Sc155), RomSlot::Wave, 0, 0x100000,
         baseLib::MD5("5f40d5297f47358ddea43b8d225874cc"), "R15209276", false},
        {romDevices(RomDevice::Sc55Mk1, RomDevice::Sc155), RomSlot::Wave, 1, 0x100000,
         baseLib::MD5("f78fb079a7d8c5f1eb04e0fd5aedbc9e"), "R15209277", false},
        {romDevices(RomDevice::Sc55Mk1, RomDevice::Sc155), RomSlot::Wave, 2, 0x100000,
         baseLib::MD5("53c013d4bad0337385cfda2631e5b78e"), "R15209281", false},

        // Additional SC-55-family dumps present in the reference checkout.
        // CM-300/SCC-1 PCM dumps match MAME's SHA-1 references for these chips;
        // the SCC-1A carries the same three.
        {romDevices(RomDevice::Cm300, RomDevice::Scc1a), RomSlot::Wave, 0, 0x100000,
         baseLib::MD5("1e8c539412480c3c261d82e049a8bb10"), "R15279806", false},
        {romDevices(RomDevice::Cm300, RomDevice::Scc1a), RomSlot::Wave, 1, 0x100000,
         baseLib::MD5("fb28743cef3fa73542554f68a8e2297f"), "R15279807", false},
        {romDevices(RomDevice::Cm300, RomDevice::Scc1a), RomSlot::Wave, 2, 0x100000,
         baseLib::MD5("f86e0433a11707a048a8a79e8d98a3be"), "R15279808", false},
        // CM-300/SCC-1 firmware 1.20 and 1.10 (they state "GS Standard VER=1.xx").
        // Both run on MCU ROM R15199774, which has not been dumped here.
        {romDevices(RomDevice::Cm300), RomSlot::Program, 0, 0x40000, baseLib::MD5("437d9575678bdcc30c2f0e1a32765027"),
         "1.20, R15279812", false},
        {romDevices(RomDevice::Cm300), RomSlot::Program, 0, 0x40000, baseLib::MD5("b2c215cdf5325ec84ecde86a5d3d1eb8"),
         "1.10, R15279809", false},
        // The SCC-1A, the card's later revision: its own MCU ROM and firmware 1.30.
        {romDevices(RomDevice::Scc1a), RomSlot::Internal, 0, 0x8000, baseLib::MD5("c40042f1469eec55870391f131c96230"),
         "R00128523", false},
        {romDevices(RomDevice::Scc1a), RomSlot::Program, 0, 0x40000, baseLib::MD5("cfbd838e3fe6bcfb92f2587fafee048e"),
         "1.30, R00128567", false},
        {romDevices(RomDevice::Scb55, RomDevice::Rlp3237), RomSlot::Internal, 0, 0x8000,
         baseLib::MD5("31883de733ad34858bfd262b8fcf2fb6"), "R15199827", false},
        {romDevices(RomDevice::Scb55), RomSlot::Program, 0, 0x40000, baseLib::MD5("89d8263a413326249c042da8bcd0fda1"),
         "R15279828", false},
        {romDevices(RomDevice::Sc155), RomSlot::Internal, 0, 0x8000, baseLib::MD5("6b74988a79c2a48239809f07a54175b3"),
         "R15199799", false},
        {romDevices(RomDevice::Sc155), RomSlot::Program, 0, 0x40000, baseLib::MD5("5c7c6ab34ef6da079b05a36fd23c9c91"),
         "SC-155 control", false},

        // -----------------------------------------------------------------
    };

    inline constexpr size_t g_romRegistrySize = sizeof(g_romRegistry) / sizeof(g_romRegistry[0]);

    constexpr bool usedBy(const RomRegistryEntry& _entry, const RomDevice _device)
    {
        return (_entry.devices & romDeviceBit(_device)) != 0;
    }

    // Human-readable slot name, for the "these images are missing" notice.
    constexpr const char* toString(const RomSlot _slot)
    {
        switch (_slot)
        {
        case RomSlot::Control:
            return "Control ROM";
        case RomSlot::Internal:
            return "Internal ROM";
        case RomSlot::Program:
            return "Program ROM";
        case RomSlot::Data:
            return "Data ROM";
        case RomSlot::Wave:
            return "Wave ROM";
        default:
            return "ROM";
        }
    }

    // Board name. Kept here rather than taken from DeviceProfile because the
    // registry covers two boards that have no DeviceModel.
    constexpr const char* toString(const RomDevice _device)
    {
        switch (_device)
        {
        case RomDevice::Sc88:
            return "SC-88";
        case RomDevice::Sc88VL:
            return "SC-88VL";
        case RomDevice::Sc88Pro:
            return "SC-88Pro";
        case RomDevice::VeGsPro:
            return "VE-GS Pro";
        case RomDevice::Sc8850:
            return "SC-8850";
        case RomDevice::Sc55Mk2:
            return "SC-55mkII";
        case RomDevice::Xpgs:
            return "XPGS / G-800";
        case RomDevice::Sc8820:
            return "SC-8820";
        case RomDevice::Sc55Mk1:
            return "SC-55";
        case RomDevice::Sc55St:
            return "SC-55st";
        case RomDevice::Cm300:
            return "CM-300 / SCC-1";
        case RomDevice::Scc1a:
            return "SCC-1A";
        case RomDevice::Cm32p:
            return "CM-32P";
        case RomDevice::Scb55:
            return "SCB-55";
        case RomDevice::Rlp3237:
            return "RLP-3237";
        case RomDevice::Sc155:
            return "SC-155";
        case RomDevice::Sc155Mk2:
            return "SC-155mkII";
        default:
            return "unknown";
        }
    }

    // The first board an entry belongs to. Shared images name several; the
    // first is the one they are described by.
    constexpr RomDevice primaryDevice(const RomRegistryEntry& _entry)
    {
        for (uint8_t i = 0; i < static_cast<uint8_t>(RomDevice::Count); ++i)
        {
            if (_entry.devices & static_cast<RomDevices>(1u << i))
                return static_cast<RomDevice>(i);
        }
        return RomDevice::Sc88;
    }

    // Identifies a row the way it is shown to the user, e.g. "SC-88VL 1.04" or
    // "SC-88 Wave ROM 2 (IC327)".
    inline std::string describe(const RomRegistryEntry& _entry)
    {
        std::string result = toString(primaryDevice(_entry));

        if (_entry.slot != RomSlot::Control)
        {
            result += ' ';
            result += toString(_entry.slot);
            // Number slots that require multiple images on an accepting board.
            for (const auto& other : g_romRegistry)
            {
                if (&other != &_entry && (other.devices & _entry.devices) != 0 && other.slot == _entry.slot &&
                    other.index != _entry.index)
                {
                    result += ' ' + std::to_string(_entry.index);
                    break;
                }
            }
        }

        if (_entry.version && *_entry.version)
            result += _entry.slot == RomSlot::Control ? std::string(" ") + _entry.version
                                                      : std::string(" (") + _entry.version + ')';
        return result;
    }

    // Looks a candidate up by content. _wordSwappedOnly restricts the search to
    // rows whose dump is known to circulate byte-swapped, so a normalization is
    // never invented for a ROM that does not need one.
    inline const RomRegistryEntry* findRegistryEntry(const size_t _size, const baseLib::MD5& _hash,
                                                     const bool _wordSwappedOnly = false)
    {
        for (const auto& entry : g_romRegistry)
        {
            if (entry.size != _size || (_wordSwappedOnly && !entry.wordSwapped))
                continue;
            if (entry.hash == _hash)
                return &entry;
        }
        return nullptr;
    }
} // namespace emu88Lib

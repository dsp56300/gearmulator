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
	// Every dump the loader accepts is described here, and nowhere else. ROMs
	// are identified purely by size and content hash: filenames are not part of
	// the contract, so a dump can be renamed, kept in a subfolder or stored
	// alongside an unrelated collection and still resolve to the right slot.
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

		Count
	};

	// Shared images name all accepting boards in one registry entry.
	using RomDevices = uint16_t;

	constexpr RomDevices romDeviceBit(const RomDevice _device)
	{
		return static_cast<RomDevices>(1u << static_cast<uint8_t>(_device));
	}

	template<typename... Ts>
	constexpr RomDevices romDevices(const Ts... _devices)
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
		// MD5 over the *canonical* image, which for a byte-swapped dump means
		// after normalization - see wordSwapped below.
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
	};

	// The SC-88 and SC-88VL run different firmware on the same board, and the
	// difference is not cosmetic: P6DR is the LCD power line on the VL only, so
	// booting one image as the other blanks the display. Content addressing is
	// what keeps them apart.
	inline constexpr RomRegistryEntry g_romRegistry[] =
	{
		// -----------------------------------------------------------------
		// SC-88 / SC-88VL
		// -----------------------------------------------------------------
		// Both state their revision as two bytes following the model name near
		// the end of the image ("SC-88 Ver101" / 01 01, and 01 04 for the VL).
		// The VL dump in circulation is byte-swapped, the SC-88's is not.
		{romDevices(RomDevice::Sc88), RomSlot::Control, 0, 0x80000,
		 baseLib::MD5("0ac771782ea58a53af590ebdf140d517"), "1.01", true},
		{romDevices(RomDevice::Sc88VL), RomSlot::Control, 0, 0x80000,
		 baseLib::MD5("25e016e93c8a44ba3c35584462b56d72"), "1.04", true},

		// The four internal PCM chips, in bank order.
		{romDevices(RomDevice::Sc88, RomDevice::Sc88VL), RomSlot::Wave, 0, 0x200000,
		 baseLib::MD5("6a92b7de3ac7b8205d29ec4497644beb"), "IC325", false},
		{romDevices(RomDevice::Sc88, RomDevice::Sc88VL), RomSlot::Wave, 1, 0x200000,
		 baseLib::MD5("d98f4b255d3a7dc830d92c71c25ce2eb"), "IC326", false},
		{romDevices(RomDevice::Sc88, RomDevice::Sc88VL), RomSlot::Wave, 2, 0x200000,
		 baseLib::MD5("c05b103d4db110b3962431173cc72967"), "IC327", false},
		{romDevices(RomDevice::Sc88, RomDevice::Sc88VL), RomSlot::Wave, 3, 0x200000,
		 baseLib::MD5("bccc26c34cac0d5509e8efb645043b5b"), "IC328", false},

		// -----------------------------------------------------------------
		// SC-88Pro
		// -----------------------------------------------------------------
		// Three control ROMs run on this board and are listed in preference
		// order: the SC-88Pro's own firmware, which states "Ver1.02" in its
		// service screen data, then the two VE-GSPro expansion-board dumps.
		// Those identify as "SC-GS" rather than SC-88Pro and carry no version
		// string, so they are named by the revision letter and year in the
		// production-board record that follows the board name.
		//
		// Its three wave ROMs are the VE-GSPro sample set, by part number.
		{romDevices(RomDevice::Sc88Pro), RomSlot::Control, 0, 0x100000,
		 baseLib::MD5("9d4c2f123b4451d8ee75c3b982760f28"), "1.02", true},
		{romDevices(RomDevice::Sc88Pro), RomSlot::Control, 0, 0x100000,
		 baseLib::MD5("784b3ea762b5f96cabdceb33d121d5e4"), "VE-GSPro A '96", true},
		{romDevices(RomDevice::Sc88Pro), RomSlot::Control, 0, 0x100000,
		 baseLib::MD5("f836d9491075c28c1c0587d4876cec65"), "VE-GSPro B '97", true},
		{romDevices(RomDevice::Sc88Pro), RomSlot::Wave, 0, 0x800000,
		 baseLib::MD5("bd33b20bb5e8f51e436e141f81a75c14"), "R01567167, XP-GS 1.01", false},
		{romDevices(RomDevice::Sc88Pro), RomSlot::Wave, 1, 0x800000,
		 baseLib::MD5("125ea2056dc208f04a141b3dcdffdb1b"), "R01567178, SC-GS 1.00", false},
		{romDevices(RomDevice::Sc88Pro), RomSlot::Wave, 2, 0x400000,
		 baseLib::MD5("48c3887c9a2a574b907242640fa0a320"), "R01233667, SC-GS 1.00", false},

		// -----------------------------------------------------------------
		// SC-8850
		// -----------------------------------------------------------------
		// The SH7016's on-chip ROM, the CS0 executable flash and the CS3
		// data/tone flash. The wave dump is one image covering both 128-Mbit
		// mask ROMs (IC53 then IC54); the loader splits it.
		{romDevices(RomDevice::Sc8850), RomSlot::Internal, 0, 0x10000,
		 baseLib::MD5("efe1ffb0ccbe1b2ec454692c522494fc"), "SH7016 boot", false},
		{romDevices(RomDevice::Sc8850), RomSlot::Program, 0, 0x100000,
		 baseLib::MD5("554d5997dcd9ce6fa0777092ff48f6fa"), "XP-GS 1.01 / SC-GS 1.00", false},
		{romDevices(RomDevice::Sc8850), RomSlot::Data, 0, 0x200000,
		 baseLib::MD5("06eee65647b66109efb01eabd6d71248"), "tone flash", false},
		{romDevices(RomDevice::Sc8850), RomSlot::Wave, 0, 0x2000000,
		 baseLib::MD5("e8eb6ac0e394997dfbc17064f9f4a70e"), "IC53+IC54, unscrambled", false},

		// -----------------------------------------------------------------
		// SC-8820
		// -----------------------------------------------------------------
		// The first 20 MiB supplies the exact SC-88Pro waves. SC-8850 still
		// requires its native wave image above; the extensions differ.
		{romDevices(RomDevice::Sc8820), RomSlot::Wave, 0, 0x1000000,
		 baseLib::MD5("2ce0dfb99b0fbe4313b225d37d68ac95"), "CS0", false},
		{romDevices(RomDevice::Sc8820), RomSlot::Wave, 1, 0x800000,
		 baseLib::MD5("35551cfb0cb36b95de6301a617249178"), "CS1", false},

		// -----------------------------------------------------------------
		// SC-55mk2
		// -----------------------------------------------------------------
		// The H8/532's on-chip boot ROM, the program ROM and the two raw PCM
		// mask-ROM dumps the board de-scrambles itself. The program ROM states
		// the GS spec level ("GS-28 VER=2.00") rather than a firmware revision.
		{romDevices(RomDevice::Sc55Mk2), RomSlot::Internal, 0, 0x8000,
		 baseLib::MD5("4ca058f7db05f51e97bb30a162e9610a"), "H8/532 boot", false},
		{romDevices(RomDevice::Sc55Mk2), RomSlot::Program, 0, 0x80000,
		 baseLib::MD5("63b24c7193ce34afefce9cec32ac39f0"), "GS-28 2.00", false},
		{romDevices(RomDevice::Sc55Mk2), RomSlot::Wave, 0, 0x200000,
		 baseLib::MD5("30df645acf7f1b621d5f2891ea53e00b"), "PCM 1", false},
		{romDevices(RomDevice::Sc55Mk2), RomSlot::Wave, 1, 0x100000,
		 baseLib::MD5("f86e0433a11707a048a8a79e8d98a3be"), "PCM 2", false},

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
		switch(_slot)
		{
		case RomSlot::Control:  return "Control ROM";
		case RomSlot::Internal: return "Internal ROM";
		case RomSlot::Program:  return "Program ROM";
		case RomSlot::Data:     return "Data ROM";
		case RomSlot::Wave:     return "Wave ROM";
		default:                return "ROM";
		}
	}

	// Board name. Kept here rather than taken from DeviceProfile because the
	// registry covers two boards that have no DeviceModel.
	constexpr const char* toString(const RomDevice _device)
	{
		switch(_device)
		{
		case RomDevice::Sc88:    return "SC-88";
		case RomDevice::Sc88VL:  return "SC-88VL";
		case RomDevice::Sc88Pro: return "SC-88Pro";
		case RomDevice::Sc8850:  return "SC-8850";
		case RomDevice::Sc55Mk2: return "SC-55mk2";
		case RomDevice::Sc8820:  return "SC-8820";
		default:                 return "unknown";
		}
	}

	// The first board an entry belongs to. Shared images name several; the
	// first is the one they are described by.
	constexpr RomDevice primaryDevice(const RomRegistryEntry& _entry)
	{
		for(uint8_t i = 0; i < static_cast<uint8_t>(RomDevice::Count); ++i)
		{
			if(_entry.devices & static_cast<RomDevices>(1u << i))
				return static_cast<RomDevice>(i);
		}
		return RomDevice::Sc88;
	}

	// Identifies a row the way it is shown to the user, e.g. "SC-88VL 1.04" or
	// "SC-88 Wave ROM 2 (IC327)".
	inline std::string describe(const RomRegistryEntry& _entry)
	{
		std::string result = toString(primaryDevice(_entry));

		if(_entry.slot != RomSlot::Control)
		{
			result += ' ';
			result += toString(_entry.slot);
			// Number slots that require multiple images on an accepting board.
			for(const auto& other : g_romRegistry)
			{
				if(&other != &_entry && (other.devices & _entry.devices) != 0 &&
				   other.slot == _entry.slot && other.index != _entry.index)
				{
					result += ' ' + std::to_string(_entry.index);
					break;
				}
			}
		}

		if(_entry.version && *_entry.version)
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
		for(const auto& entry : g_romRegistry)
		{
			if(entry.size != _size || (_wordSwappedOnly && !entry.wordSwapped))
				continue;
			if(entry.hash == _hash)
				return &entry;
		}
		return nullptr;
	}
}

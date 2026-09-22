#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "synthLib/midi/songReset.h"

namespace emu88Lib
{
	enum class DeviceModel : uint8_t
	{
		Sc88 = 0,
		Sc88VL,
		Sc88Pro,
		Sc8850,
		Sc55Mk2,
		Sc55Mk1,
		Sc55St,
		Cm300,
		Scb55,
		Rlp3237,
		Sc155,
		Sc155Mk2,
		Xpgs,
		Sc8820,
		Cm32p,
		VeGsPro,
		Scc1a,
		Cm64,
		Cm32l,
		Nu10b,
		Miig5,
		Mt32Old,
		Mt32New,
		Cm32ln,
	};

	// Curated presentation order. Enum values remain
	// stable because they are persisted in plugin settings and project state.
	inline constexpr std::array<DeviceModel, 24> g_deviceMenuOrder = {
		DeviceModel::Mt32Old,
		DeviceModel::Mt32New,
		DeviceModel::Cm32l,
		DeviceModel::Cm32ln,
		DeviceModel::Cm32p,
		DeviceModel::Cm64,
		DeviceModel::Sc55Mk1,
		DeviceModel::Sc55Mk2,
		DeviceModel::Sc55St,
		DeviceModel::Cm300,
		DeviceModel::Scc1a,
		DeviceModel::Scb55,
		DeviceModel::Rlp3237,
		DeviceModel::Sc155,
		DeviceModel::Sc155Mk2,
		DeviceModel::Sc88,
		DeviceModel::Sc88VL,
		DeviceModel::Xpgs,
		DeviceModel::Sc88Pro,
		DeviceModel::VeGsPro,
		DeviceModel::Sc8820,
		DeviceModel::Sc8850,
		DeviceModel::Nu10b,
		DeviceModel::Miig5,
	};

	struct DeviceProfile
	{
		const char* displayName;
		uint8_t groupCount;
	};

	enum class Sc55Generation : uint8_t { First, Mk2 };
	enum class Sc55MidiFrontend : uint8_t { DirectH8, SubMcu };
	enum class Sc55Panel : uint8_t { None, Sc55, Sc155 };

	struct Sc55DeviceProfile
	{
		Sc55Generation generation;
		Sc55MidiFrontend midiFrontend;
		Sc55Panel panel;
		size_t programSize;
		std::array<size_t, 3> waveSizes;
		// Raw ROM slot logical GP bank for each Wave slot.
		std::array<uint8_t, 3> waveBanks;
		uint8_t p9Strap;
	};

	constexpr bool isSc55Model(const DeviceModel _model)
	{
		return _model == DeviceModel::Sc55Mk1 || _model == DeviceModel::Sc55Mk2 ||
		       _model == DeviceModel::Sc55St || _model == DeviceModel::Cm300 ||
		       _model == DeviceModel::Scb55 || _model == DeviceModel::Rlp3237 ||
		       _model == DeviceModel::Sc155 || _model == DeviceModel::Sc155Mk2 || _model == DeviceModel::Scc1a;
	}

	constexpr Sc55DeviceProfile getSc55DeviceProfile(const DeviceModel _model)
	{
		switch(_model)
		{
		case DeviceModel::Sc55Mk1:
			return {Sc55Generation::First, Sc55MidiFrontend::DirectH8, Sc55Panel::Sc55,
			        0x40000, {0x100000, 0x100000, 0x100000}, {0, 1, 2}, 0};
		case DeviceModel::Cm300:
		case DeviceModel::Scc1a:
			return {Sc55Generation::First, Sc55MidiFrontend::DirectH8, Sc55Panel::None,
			        0x40000, {0x100000, 0x100000, 0x100000}, {0, 1, 2}, 0};
		case DeviceModel::Sc155:
			return {Sc55Generation::First, Sc55MidiFrontend::DirectH8, Sc55Panel::Sc155,
			        0x40000, {0x100000, 0x100000, 0x100000}, {0, 1, 2}, 0};
		case DeviceModel::Sc55St:
			return {Sc55Generation::Mk2, Sc55MidiFrontend::SubMcu, Sc55Panel::None,
			        0x80000, {0x200000, 0x100000, 0}, {0, 1, 0}, 2};
		case DeviceModel::Sc155Mk2:
			return {Sc55Generation::Mk2, Sc55MidiFrontend::SubMcu, Sc55Panel::Sc155,
			        0x80000, {0x200000, 0x100000, 0}, {0, 1, 0}, 0};
		case DeviceModel::Scb55:
			return {Sc55Generation::Mk2, Sc55MidiFrontend::DirectH8, Sc55Panel::None,
			        0x40000, {0x200000, 0x100000, 0}, {0, 2, 0}, 2};
		case DeviceModel::Rlp3237:
			return {Sc55Generation::Mk2, Sc55MidiFrontend::DirectH8, Sc55Panel::None,
			        0x40000, {0x200000, 0, 0}, {0, 0, 0}, 2};
		case DeviceModel::Sc55Mk2:
		default:
			return {Sc55Generation::Mk2, Sc55MidiFrontend::SubMcu, Sc55Panel::Sc55,
			        0x80000, {0x200000, 0x100000, 0}, {0, 1, 0}, 2};
		}
	}

	// Which of Roland's LA boards a device is, for the boards that are one (the CM-64 is a
	// CM-32L board with a CM-32P beside it). The MT-32 comes in two boards that need their
	// own firmware and reverb microcode; the CM-32LN, the CM-500's LA half and the LAPC-N
	// are a CM-32L on an 80C198 that runs two clocks per state instead of three.
	enum class LaModel : uint8_t
	{
		Mt32Old,
		Mt32New,
		Cm32l,
		Cm32ln,
	};

	// The devices that are one LA board and nothing else.
	constexpr bool isLaModel(const DeviceModel _model)
	{
		return _model == DeviceModel::Mt32Old || _model == DeviceModel::Mt32New || _model == DeviceModel::Cm32l ||
		       _model == DeviceModel::Cm32ln;
	}

	// The LA board inside a device, for isLaModel() devices and the CM-64.
	constexpr LaModel toLaModel(const DeviceModel _model)
	{
		switch(_model)
		{
		case DeviceModel::Mt32Old: return LaModel::Mt32Old;
		case DeviceModel::Mt32New: return LaModel::Mt32New;
		case DeviceModel::Cm32ln: return LaModel::Cm32ln;
		default: return LaModel::Cm32l;
		}
	}

	// The devices built on Roland's LA and CM boards: the MT-32s, the CM-32L family, the
	// CM-32P and the CM-64 that pairs the two. They share a MIDI dialect that predates GM:
	// the GM, GM2 and GS System On messages mean nothing to them, they reset on their own
	// "all parameters reset" (a data set to address 7F 00 00) instead, and they know
	// All Notes Off but not All Sound Off.
	constexpr bool isRolandLaFamily(const DeviceModel _model)
	{
		return isLaModel(_model) || _model == DeviceModel::Cm32p || _model == DeviceModel::Cm64;
	}

	// The boards with the MT-32's VOLUME/VALUE knob, a potentiometer the firmware reads
	// through the CPU's A/D converter.
	constexpr bool hasPanelKnob(const DeviceModel _model)
	{
		return _model == DeviceModel::Mt32Old || _model == DeviceModel::Mt32New;
	}

	// The lowest MIDI channel a board answers on from power-on, 0-based: the LA boards put
	// part 1 on channel 2 (the rhythm part is on 10), the CM-32P's six parts start on 11.
	// The CM-64 is both at once and starts with its LA half.
	constexpr uint8_t firstMidiChannel(const DeviceModel _model)
	{
		if(_model == DeviceModel::Cm32p)
			return 10;
		return isLaModel(_model) || _model == DeviceModel::Cm64 ? 1 : 0;
	}

	// The boards wearing the CM bezel: the CM-32L and CM-32LN, the CM-32P, and the CM-64 that
	// pairs that PCM board with a CM-32L. They share artwork and the front-panel lamp; the
	// halves differ in display geometry, 16x2 on the CM-32P and 20x1 on the CM-32L.
	constexpr bool isCmModel(const DeviceModel _model)
	{
		return _model == DeviceModel::Cm32p || _model == DeviceModel::Cm64 || _model == DeviceModel::Cm32l ||
		       _model == DeviceModel::Cm32ln;
	}

	// The boards with a slot for an SN-U110 series PCM card: the CM-32P, and the CM-64
	// built on it.
	constexpr bool hasPcmCardSlot(const DeviceModel _model)
	{
		return _model == DeviceModel::Cm32p || _model == DeviceModel::Cm64;
	}

	// Modules that run in their GM mode and nothing else: construction sets them up, and they
	// have no panel or display here.
	constexpr bool isGmModuleModel(const DeviceModel _model)
	{
		return _model == DeviceModel::Nu10b || _model == DeviceModel::Miig5;
	}

	// The CM-32L's display is a service screen the case has no window for: the firmware
	// drives its SED1200 whether or not one is attached, and those screens are the only way
	// to read the board's state, so it is shown here the way the CM-32P's is. On the MT-32
	// the same display is the front panel's.
	constexpr bool deviceHasLcd(const DeviceModel model)
	{
		return model != DeviceModel::Xpgs && model != DeviceModel::VeGsPro && model != DeviceModel::Sc8820 &&
		       !isGmModuleModel(model) && (!isSc55Model(model) || getSc55DeviceProfile(model).panel != Sc55Panel::None);
	}

	// Whether a model appears in the device menu and the CLI's device list. Every board is
	// listed now that the SC-55st, RLP-3237 and CM-300/SCC-1 images are catalogued; the
	// hook stays for the next board whose dump is still out.
	constexpr bool isDeviceListed(const DeviceModel)
	{
		return true;
	}

	// The CM-64 carries two service displays, one per board: the CM-32P's 16x2 above the
	// CM-32L's 20x1. Every other device drives one at most.
	constexpr bool deviceHasSecondLcd(const DeviceModel _model)
	{
		return _model == DeviceModel::Cm64;
	}

	// What the front-panel POWER switch does.
	//
	// Standby: it is matrix position 0, a key like any other, and the firmware keeps running on the supply. A
	// press puts the unit in standby - display supply cut, lamps dark, voices silenced, incoming MIDI still read
	// and thrown away - and the next press wakes it, reading the other keys held as it does, so the manuals'
	// hold-and-power-on combinations are made with this switch. Only cutting the supply itself cold-boots the
	// board. On the SC-88VL the firmware's standby is 01:990A and its wake-up 01:98EC.
	// The SC-155mkII runs the SC-55mkII program.
	//
	// Supply: the switch cuts the supply, or the board has no switch of its own and runs on its host's. The plain
	// SC-88Pro and SC-8850 service schematics put POWER in the mains feed. The Pro firmware still has
	// a standby handler, but its physical switch is not wired to the panel matrix. Classify the hardware,
	// not the presence of a firmware handler. SC-55st also has an on/off switch, unlike SC-55/mkII.
	enum class PowerSwitch : uint8_t { Supply, Standby };

	constexpr PowerSwitch getPowerSwitch(const DeviceModel _model)
	{
		switch(_model)
		{
		case DeviceModel::Sc55Mk1:
		case DeviceModel::Sc55Mk2:
		case DeviceModel::Sc155:
		case DeviceModel::Sc155Mk2:
		case DeviceModel::Sc88VL:
			return PowerSwitch::Standby;
		default:
			return PowerSwitch::Supply;
		}
	}

	const DeviceProfile& getDeviceProfile(DeviceModel _model);
	bool isDeviceModelValue(uint32_t _value);
	uint32_t deviceModelCount();

	// The reset a device answers to, see synthLib::midi::ResetTarget.
	synthLib::midi::ResetTarget resetTarget(DeviceModel _model);
	// The device's own reset message: GS Reset, or the LA and CM boards' all parameters reset.
	std::vector<uint8_t> deviceResetSysex(DeviceModel _model);
}

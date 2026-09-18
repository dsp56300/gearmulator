#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
	};

	// Curated presentation order. Enum values remain
	// stable because they are persisted in plugin settings and project state.
	inline constexpr std::array<DeviceModel, 19> g_deviceMenuOrder = {
		DeviceModel::Cm32l,
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

	// The boards wearing the CM bezel: the CM-32L, the CM-32P, and the CM-64 that pairs
	// that PCM board with a CM-32L. They share artwork and the front-panel lamp; the two
	// halves differ in display geometry, 16x2 on the CM-32P and 20x1 on the CM-32L.
	constexpr bool isCmModel(const DeviceModel _model)
	{
		return _model == DeviceModel::Cm32p || _model == DeviceModel::Cm64 || _model == DeviceModel::Cm32l;
	}

	// The CM-32L's display is a service screen the case has no window for: the firmware
	// drives its SED1200 whether or not one is attached, and those screens are the only way
	// to read the board's state, so it is shown here the way the CM-32P's is.
	constexpr bool deviceHasLcd(const DeviceModel model)
	{
		return model != DeviceModel::Xpgs && model != DeviceModel::VeGsPro && model != DeviceModel::Sc8820 && (!isSc55Model(model) || getSc55DeviceProfile(model).panel != Sc55Panel::None);
	}

	// Left out of the device menu and the CLI's device list for now, for want of a
	// dump: the SC-55st and RLP-3237 program ROMs, and the MCU ROM (R15199774) the
	// CM-300/SCC-1 firmware runs on. The IDs and enum values stay, so an explicit
	// --device still selects them.
	//
	// The CM-32L and the CM-64 built on it are hidden for the same reason from the
	// other end: the ROMs are there but the emulation is not finished yet.
	constexpr bool isDeviceListed(const DeviceModel _model)
	{
		return _model != DeviceModel::Sc55St && _model != DeviceModel::Rlp3237 &&
		       _model != DeviceModel::Cm300 && _model != DeviceModel::Cm32l &&
		       _model != DeviceModel::Cm64;
	}

	// The CM-64 carries two service displays, one per board: the CM-32P's 16x2 above the
	// CM-32L's 20x1. Every other device drives one at most.
	constexpr bool deviceHasSecondLcd(const DeviceModel _model)
	{
		return _model == DeviceModel::Cm64;
	}

	const DeviceProfile& getDeviceProfile(DeviceModel _model);
	bool isDeviceModelValue(uint32_t _value);
	uint32_t deviceModelCount();
}

#include "88lib/deviceModel.h"

#include <array>
#include <cstddef>

namespace emu88Lib
{
	namespace
	{
		// Indexed by DeviceModel. The keys are part of the public interface (see DeviceProfile):
		// never change one, even when the display name changes.
		constexpr std::array<DeviceProfile, 28> g_profiles = {{
			{"SC-88", "sc88", 2},
			{"SC-88VL", "sc88vl", 2},
			{"SC-88Pro", "sc88pro", 2},
			{"SC-8850", "sc8850", 4},
			{"SC-55mkII", "sc55mkii", 1},
			{"SC-55", "sc55", 1},
			{"SC-55st", "sc55st", 1},
			{"CM-300 / SCC-1", "cm300scc1", 1},
			{"SCB-55", "scb55", 1},
			{"RLP-3237", "rlp3237", 1},
			{"SC-155", "sc155", 1},
			{"SC-155mkII", "sc155mkii", 1},
			{"XPGS / G-800", "xpgsg800", 2},
			{"SC-8820", "sc8820", 2},
			{"CM-32P", "cm32p", 1},
			{"VE-GS Pro", "vegspro", 2},
			{"SCC-1A", "scc1a", 1},
			{"CM-64", "cm64", 1},
			{"CM-32L", "cm32l", 1},
			{"JV-1080 (GM mode)", "nu10b", 1},
			{"XV-3080 (GM mode)", "miig5", 1},
			{"MT-32 1.x (experimental)", "mt321x", 1},
			{"MT-32 2.x", "mt322x", 1},
			{"CM-32LN (experimental)", "cm32ln", 1},
			{"MV-30 (experimental)", "mv30", 1},
			{"SK-88Pro", "sk88pro", 2},
			{"SC-880", "sc880", 2},
			{"PRO-E", "proe", 1},
		}};
	}

	const DeviceProfile& getDeviceProfile(const DeviceModel _model)
	{
		const auto index = static_cast<size_t>(_model);
		return index < g_profiles.size() ? g_profiles[index] : g_profiles.front();
	}

	bool isDeviceModelValue(const uint32_t _value)
	{
		return _value < g_profiles.size();
	}

	uint32_t deviceModelCount()
	{
		return static_cast<uint32_t>(g_profiles.size());
	}

	synthLib::midi::ResetTarget resetTarget(const DeviceModel _model)
	{
		return isRolandLaFamily(_model) ? synthLib::midi::ResetTarget::RolandLaModule
		                                : synthLib::midi::ResetTarget::GsModule;
	}

	std::vector<uint8_t> deviceResetSysex(const DeviceModel _model)
	{
		const auto& sysex = isRolandLaFamily(_model) ? synthLib::midi::kRolandLaResetSysex : synthLib::midi::kGsResetSysex;
		return {sysex.begin(), sysex.end()};
	}

}

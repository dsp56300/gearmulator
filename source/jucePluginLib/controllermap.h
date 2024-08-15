#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "parameter.h"
#include "synthLib/midiTypes.h"

namespace pluginLib
{
	class ControllerMap
	{
	public:
		void add(synthLib::MidiStatusByte _midiStatusByte, uint8_t _cc, uint32_t _paramIndex);

		const std::vector<uint32_t>& getControlledParameters(const synthLib::SMidiEvent& _ev) const;
		std::vector<uint8_t> getControlChanges(synthLib::MidiStatusByte _midiStatusByte, uint32_t _paramIndex) const;

	private:
		std::unordered_map<uint8_t, std::unordered_map<uint8_t, std::vector<uint32_t>>> m_ccToParamIndex;	// type (control change, poly pressure) => index (modwheel, main vol, ...) => parameter index
		std::unordered_map<uint8_t, std::unordered_map<uint32_t, std::vector<uint8_t>>> m_paramIndexToCC;	// type (control change, poly pressure) => parameter index => index (modwheel, main vol, ...)
	};
}

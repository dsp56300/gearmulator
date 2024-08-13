#include "controllermap.h"

namespace pluginLib
{
	void ControllerMap::add(synthLib::MidiStatusByte _midiStatusByte, uint8_t _cc, uint32_t _paramIndex)
	{
		m_ccToParamIndex[_midiStatusByte][_cc].push_back(_paramIndex);
		m_paramIndexToCC[_midiStatusByte][_paramIndex].push_back(_cc);
	}

	const std::vector<uint32_t>& ControllerMap::getControlledParameters(const synthLib::SMidiEvent& _ev) const
	{
		static std::vector<uint32_t> empty;
		const uint8_t type = _ev.a & 0xf0;

		const auto itType = m_ccToParamIndex.find(type);
		if(itType == m_ccToParamIndex.end())
			return empty;

		const auto itValue = itType->second.find(_ev.b);
		if(itValue == itType->second.end())
			return empty;

		return itValue->second;
	}

	std::vector<uint8_t> ControllerMap::getControlChanges(const synthLib::MidiStatusByte _midiStatusByte, const uint32_t _paramIndex) const
	{
		static std::vector<uint8_t> empty;
		const auto itType = m_paramIndexToCC.find(_midiStatusByte);
		if(itType == m_paramIndexToCC.end())
			return empty;
		const auto itCCList = itType->second.find(_paramIndex);
		if(itCCList == itType->second.end())
			return empty;
		return itCCList->second;
	}
}

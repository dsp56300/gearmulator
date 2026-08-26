#include "controllermap.h"

namespace pluginLib
{
	void ControllerMap::add(const MessageType _messageType, const ControlType _controlType, const ParamIndex _paramIndex)
	{
		auto& maps = m_mapsPerMessageType[_messageType];

		maps.ccToParamIndex[_controlType].push_back(_paramIndex);
		maps.paramIndexToCC[_paramIndex].push_back(_controlType);
	}

	const std::vector<uint32_t>& ControllerMap::getParameters(const synthLib::SMidiEvent& _ev) const
	{
		static std::vector<uint32_t> empty;
		const auto type = static_cast<MessageType>(_ev.a & 0xf0);

		const auto itType = m_mapsPerMessageType.find(type);
		if(itType == m_mapsPerMessageType.end())
			return empty;

		const auto itValue = itType->second.ccToParamIndex.find(_ev.b);
		if(itValue == itType->second.ccToParamIndex.end())
			return empty;

		return itValue->second;
	}
	
	const std::vector<uint32_t>& ControllerMap::getParameters(const uint8_t _nrpnMsb, const uint8_t _nrpnLsb) const
	{
		static std::vector<uint32_t> empty;

		const auto itType = m_mapsPerMessageType.find(NrpnType);

		if (itType == m_mapsPerMessageType.end())
			return empty;

		const auto n = nrpn(_nrpnMsb, _nrpnLsb);

		const auto itValue = itType->second.ccToParamIndex.find(n);

		if (itValue == itType->second.ccToParamIndex.end())
			return empty;

		return itValue->second;
	}

	std::vector<ControllerMap::ControlType> ControllerMap::getControlTypes(const MessageType _midiStatusByte, const ParamIndex _paramIndex) const
	{
		static std::vector<ControlType> empty;

		const auto itType = m_mapsPerMessageType.find(_midiStatusByte);
		if (itType == m_mapsPerMessageType.end())
			return empty;
		const auto itCCList = itType->second.paramIndexToCC.find(_paramIndex);
		if(itCCList == itType->second.paramIndexToCC.end())
			return empty;
		return itCCList->second;
	}
}

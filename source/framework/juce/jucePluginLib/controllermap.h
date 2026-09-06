#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	class ControllerMap
	{
	public:
		using ParamIndex = uint32_t;
		using ControlType = uint16_t;
		using MessageType = synthLib::MidiStatusByte;

		static constexpr synthLib::MidiStatusByte NrpnType = MessageType::M_SYSTEMRESET;

		struct TwoWayMap
		{
			std::unordered_map<ControlType, std::vector<ParamIndex>> ccToParamIndex;
			std::unordered_map<ParamIndex, std::vector<ControlType>> paramIndexToCC;
		};

		void add(MessageType _messageType, ControlType _controlType, ParamIndex _paramIndex);

		const std::vector<uint32_t>& getParameters(const synthLib::SMidiEvent& _ev) const;
		const std::vector<uint32_t>& getParameters(uint8_t _nrpnMsb, uint8_t _nrpnLsb) const;

		std::vector<ControlType> getControlTypes(MessageType _midiStatusByte, ParamIndex _paramIndex) const;

		static constexpr uint16_t nrpn(const uint8_t _nrpnMsb, const uint8_t _nrpnLsb)
		{
			return static_cast<uint16_t>(static_cast<uint16_t>(_nrpnMsb & 0x7f) << 7) | static_cast<uint16_t>(_nrpnLsb & 0x7f);
		}

	private:
		std::unordered_map<MessageType, TwoWayMap> m_mapsPerMessageType;
	};
}

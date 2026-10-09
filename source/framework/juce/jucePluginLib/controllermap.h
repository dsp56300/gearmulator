#pragma once

#include <array>
#include <cstdint>
#include <optional>
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

	// Follows the NRPN that a channel selects with CC 99/98 and reports the data entries for it, CC 6 (MSB) and
	// CC 38 (LSB). Selecting an RPN (CC 101/100) or the null NRPN 127/127 ends it
	class NrpnDecoder
	{
	public:
		struct Entry
		{
			uint8_t nrpnMsb;
			uint8_t nrpnLsb;
			uint8_t msb;
			std::optional<uint8_t> lsb;	// set for a CC 38, it refines the MSB of the CC 6 before it
		};

		std::optional<Entry> process(const synthLib::SMidiEvent& _ev);

	private:
		struct Channel
		{
			uint8_t nrpnMsb = 0x7f;
			uint8_t nrpnLsb = 0x7f;
			bool selected = false;
			uint8_t dataMsb = 0;
		};

		std::array<Channel, 16> m_channels;
	};
}

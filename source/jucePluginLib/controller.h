#pragma once

#include "parameterdescriptions.h"
#include "parameter.h"

#include <string>

namespace pluginLib
{
	class Processor;
	using SysEx = std::vector<uint8_t>;

	class Controller
	{
	public:
		static constexpr uint32_t InvalidParameterIndex = 0xffffffff;

		explicit Controller(pluginLib::Processor& _processor, const std::string& _parameterDescJson);
		virtual ~Controller() = default;

		virtual void sendParameterChange(const Parameter& _parameter, uint8_t _value) = 0;

        juce::Value* getParamValueObject(uint32_t _index);
        Parameter* getParameter(uint32_t _index) const;
        Parameter* getParameter(uint32_t _index, uint8_t _part) const;
		
        uint32_t getParameterIndexByName(const std::string& _name) const;

		const MidiPacket* getMidiPacket(const std::string& _name) const;

		bool createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _params, uint8_t _part) const;

		bool parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;
		bool parseMidiPacket(const std::string& _name, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;
		bool parseMidiPacket(std::string& _name, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;

		const auto& getExposedParameters() { return m_synthParams; }

		uint8_t getCurrentPart() const { return m_currentPart; }
		void setCurrentPart(const uint8_t _part) { m_currentPart = _part; }

		virtual void parseSysexMessage(const SysEx&) = 0;
		virtual void onStateLoaded() = 0;

	protected:
		virtual Parameter* createParameter(Controller& _controller, const Description& _desc, uint8_t _part, int _uid);
		void registerParams(juce::AudioProcessor& _processor);

		void sendSysEx(const pluginLib::SysEx &) const;
		bool sendSysEx(const std::string& _packetName) const;
		bool sendSysEx(const std::string& _packetName, const std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

	private:
		Processor& m_processor;
        ParameterDescriptions m_descriptions;

        struct ParamIndex
        {
            uint8_t page;
            uint8_t partNum;
            uint8_t paramNum;
            bool operator<(ParamIndex const &rhs) const
            {
				if (page < rhs.page)         return false;
				if (page > rhs.page)         return true;
				if (partNum < rhs.partNum)   return false;
				if (partNum > rhs.partNum)   return true;
				if (paramNum < rhs.paramNum) return false;
				if (paramNum > rhs.paramNum) return true;
				return false;
			}
        };

		using ParameterList = std::vector<Parameter*>;

		uint8_t m_currentPart = 0;

	protected:
		// tries to find synth param in both internal and host
		const ParameterList& findSynthParam(uint8_t _part, uint8_t _page, uint8_t _paramIndex);
		const ParameterList& findSynthParam(const ParamIndex& _paramIndex);

    	std::map<ParamIndex, ParameterList> m_synthInternalParams;
		std::map<ParamIndex, ParameterList> m_synthParams; // exposed and managed by audio processor
		std::array<ParameterList, 16> m_paramsByParamType;
		std::vector<std::unique_ptr<Parameter>> m_synthInternalParamList;
	};
}

#pragma once

#include "parameterdescriptions.h"
#include "parameter.h"

#include <string>

namespace pluginLib
{
	class Controller
	{
	public:
		static constexpr uint32_t InvalidParameterIndex = 0xffffffff;

		explicit Controller(const std::string& _parameterDescJson);

		virtual void sendParameterChange(const Parameter& _parameter, uint8_t _value) = 0;

        juce::Value* getParamValueObject(uint32_t _index);
        Parameter* getParameter(uint32_t _index) const;
        Parameter* getParameter(uint32_t _index, uint8_t _part) const;
		
        uint32_t getParameterIndexByName(const std::string& _name) const;

		const MidiPacket* getMidiPacket(const std::string& _name) const;
		bool createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _params) const;

		bool parseMidiPacket(const std::string& _name, std::map<pluginLib::MidiDataType, uint8_t>& _data, std::map<uint32_t, uint8_t>& _parameterValues, const std::vector<uint8_t>& _src) const;

	protected:
		virtual Parameter* createParameter(Controller& _controller, const Description& _desc, uint8_t _part, int _uid) = 0;
		void registerParams(juce::AudioProcessor& _processor);

	private:

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

		// tries to find synth param in both internal and host
	protected:
		const ParameterList& findSynthParam(uint8_t _part, uint8_t _page, uint8_t _paramIndex);
		const ParameterList& findSynthParam(const ParamIndex& _paramIndex);

    	std::map<ParamIndex, ParameterList> m_synthInternalParams;
		std::map<ParamIndex, ParameterList> m_synthParams; // exposed and managed by audio processor
		std::array<ParameterList, 16> m_paramsByParamType;
		std::vector<std::unique_ptr<Parameter>> m_synthInternalParamList;
	};
}

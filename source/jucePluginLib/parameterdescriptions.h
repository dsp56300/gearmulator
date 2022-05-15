#pragma once

#include <map>
#include <string>
#include <vector>

#include "midipacket.h"
#include "parameterdescription.h"

namespace pluginLib
{
	class ParameterDescriptions
	{
	public:
		explicit ParameterDescriptions(const std::string& _jsonString);

		const std::vector<Description>& getDescriptions() const
		{
			return m_descriptions;
		}

		const MidiPacket* getMidiPacket(const std::string& _name) const;

		static std::string removeComments(std::string _json);

		bool getIndexByName(uint32_t& _index, const std::string& _name) const;

	private:
		std::string loadJson(const std::string& _jsonString);
		void parseMidiPackets(std::stringstream& _errors, juce::DynamicObject* _packets);
		void parseMidiPacket(std::stringstream& _errors, const std::string& _key, const juce::var& _value);

		std::map<std::string, ValueList> m_valueLists;
		std::vector<Description> m_descriptions;
		std::map<std::string, MidiPacket> m_midiPackets;
	};
}

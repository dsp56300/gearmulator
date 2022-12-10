#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include "midipacket.h"
#include "parameterdescription.h"
#include "parameterlink.h"

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

		const std::unordered_map<std::string, MidiPacket>& getMidiPackets() const { return m_midiPackets; }

	private:
		std::string loadJson(const std::string& _jsonString);
		void parseMidiPackets(std::stringstream& _errors, juce::DynamicObject* _packets);
		void parseMidiPacket(std::stringstream& _errors, const std::string& _key, const juce::var& _value);

		void parseParameterLinks(std::stringstream& _errors, juce::Array<juce::var>* _links);
		void parseParameterLink(std::stringstream& _errors, const juce::var& _value);

		std::unordered_map<std::string, ValueList> m_valueLists;
		std::vector<Description> m_descriptions;
		std::unordered_map<std::string, uint32_t> m_nameToIndex;
		std::unordered_map<std::string, MidiPacket> m_midiPackets;
		std::vector<ParameterLink> m_parameterLinks;
	};
}

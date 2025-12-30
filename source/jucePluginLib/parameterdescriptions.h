#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include "controllermap.h"
#include "midipacket.h"
#include "parameterdescription.h"
#include "parameterlink.h"
#include "parameterregion.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace juce
{
	class var;
	class DynamicObject;
}

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

		const auto& getRegions() const { return m_regions; }

		const ValueList* getValueList(const std::string& _key) const;

		const std::string& getErrors() const { return m_errors; }
		bool isValid() const { return getErrors().empty(); }

		const auto& getControllerMap() const { return m_controllerMap; }

	private:
		std::string loadJson(const std::string& _jsonString);

		std::string parseValueList(const std::string& _key, const juce::var& _values);

		void parseMidiPackets(std::stringstream& _errors, juce::DynamicObject* _packets);
		void parseMidiPacket(std::stringstream& _errors, const std::string& _key, const juce::var& _value);

		void parseParameterRegions(std::stringstream& _errors, const juce::Array<juce::var>* _regions);
		void parseParameterRegion(std::stringstream& _errors, const juce::var& _value);

		void parseControllerMap(std::stringstream& _errors, const juce::Array<juce::var>* _controllers);
		void parseController(std::stringstream& _errors, const juce::var& _value);

		std::unordered_map<std::string, ValueList> m_valueLists;
		std::vector<Description> m_descriptions;
		std::unordered_map<std::string, uint32_t> m_nameToIndex;
		std::unordered_map<std::string, MidiPacket> m_midiPackets;
		std::vector<ParameterLink> m_parameterLinks;
		std::unordered_map<std::string, ParameterRegion> m_regions;
		ControllerMap m_controllerMap;

		std::string m_errors;
	};
}

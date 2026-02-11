#pragma once

#include "midiLearnMapping.h"

#include <juce_core/juce_core.h>
#include <vector>
#include <string>

namespace synthLib { struct SMidiEvent; }

namespace pluginLib
{
	class MidiLearnPreset
	{
	public:
		MidiLearnPreset() = default;
		explicit MidiLearnPreset(std::string _name);

		// Preset metadata
		const std::string& getName() const { return m_name; }
		void setName(std::string _name) { m_name = std::move(_name); }

		// Mapping management
		void addMapping(const MidiLearnMapping& _mapping);
		void removeMapping(size_t _index);
		void clearMappings();
		const std::vector<MidiLearnMapping>& getMappings() const { return m_mappings; }

		// Find mapping by MIDI message
		const MidiLearnMapping* findMapping(MidiLearnMapping::Type _type, uint8_t _channel, uint8_t _controller) const;
		const MidiLearnMapping* findMapping(const synthLib::SMidiEvent& _event) const;

		// Find mappings by parameter
		std::vector<const MidiLearnMapping*> findMappingsByParam(const std::string& _paramName) const;

		// JSON serialization
		juce::var toJson() const;
		bool fromJson(const juce::var& _json);

		// File I/O
		bool saveToFile(const juce::File& _file) const;
		bool loadFromFile(const juce::File& _file);

	private:
		std::string m_name;
		std::vector<MidiLearnMapping> m_mappings;
	};
}

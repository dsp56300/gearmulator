#pragma once

#include "midiLearnMapping.h"

#include <juce_core/juce_core.h>
#include <vector>
#include <string>

namespace synthLib { struct SMidiEvent; }

namespace pluginLib
{
	struct MidiInputBlock
	{
		std::string paramName;
		uint8_t part = MidiLearnMapping::AutoPart;

		bool operator==(const MidiInputBlock& _other) const
		{
			return paramName == _other.paramName && part == _other.part;
		}
	};

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
		std::vector<MidiLearnMapping>& getMappings() { return m_mappings; }

		// Parameter-level MIDI suppression, independent of learned mappings.
		void addInputBlock(const MidiInputBlock& _block);
		void removeInputBlock(const std::string& _paramName, uint8_t _part);
		bool isInputBlocked(const std::string& _paramName, uint8_t _part) const;
		const std::vector<MidiInputBlock>& getInputBlocks() const { return m_inputBlocks; }
		std::vector<MidiInputBlock>& getInputBlocks() { return m_inputBlocks; }

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

		// Comparison
		bool operator==(const MidiLearnPreset& _other) const;

		// Returns true if preset is empty (no mappings and no name)
		bool empty() const { return m_name.empty() && m_mappings.empty() && m_inputBlocks.empty(); }

		// Default feedback targets applied to newly learned mappings
		uint8_t getDefaultFeedbackTargets() const { return m_defaultFeedbackTargets; }
		void setDefaultFeedbackTargets(uint8_t _targets) { m_defaultFeedbackTargets = _targets; }

	private:
		std::string m_name;
		std::vector<MidiLearnMapping> m_mappings;
		std::vector<MidiInputBlock> m_inputBlocks;
		uint8_t m_defaultFeedbackTargets = 0;
	};
}

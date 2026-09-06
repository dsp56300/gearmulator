#pragma once

#include "midiLearnPreset.h"

#include <juce_core/juce_core.h>
#include <vector>

namespace pluginLib
{
	// Manages MIDI Learn presets for a specific plugin type
	// Handles preset file I/O and provides preset list management
	class MidiLearnManager
	{
	public:
		explicit MidiLearnManager(const juce::File& _presetsDirectory);

		// Preset file management
		std::vector<juce::String> getPresetNames() const;
		juce::File getPresetFile(const juce::String& _presetName) const;
		
		bool savePreset(const juce::String& _name, const MidiLearnPreset& _preset);
		bool loadPreset(const juce::String& _name, MidiLearnPreset& _preset);
		bool deletePreset(const juce::String& _name);
		bool renamePreset(const juce::String& _oldName, const juce::String& _newName);
		
		juce::String createUniquePresetName(const juce::String& _baseName) const;

		const juce::File& getPresetsDirectory() const { return m_presetsDirectory; }

	private:
		juce::File m_presetsDirectory;
	};
}

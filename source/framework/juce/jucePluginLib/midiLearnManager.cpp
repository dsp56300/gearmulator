#include "midiLearnManager.h"

#include <juce_core/juce_core.h>

namespace pluginLib
{
	MidiLearnManager::MidiLearnManager(const juce::File& _presetsDirectory)
		: m_presetsDirectory(_presetsDirectory)
	{
		// Ensure presets directory exists
		m_presetsDirectory.createDirectory();
	}

	std::vector<juce::String> MidiLearnManager::getPresetNames() const
	{
		std::vector<juce::String> names;

		if (!m_presetsDirectory.exists())
			return names;

		const auto files = m_presetsDirectory.findChildFiles(juce::File::findFiles, false, "*.json");

		for (const auto& file : files)
		{
			names.push_back(file.getFileNameWithoutExtension());
		}

		return names;
	}

	juce::File MidiLearnManager::getPresetFile(const juce::String& _presetName) const
	{
		return m_presetsDirectory.getChildFile(_presetName + ".json");
	}

	bool MidiLearnManager::savePreset(const juce::String& _name, const MidiLearnPreset& _preset)
	{
		const auto file = getPresetFile(_name);
		return _preset.saveToFile(file);
	}

	bool MidiLearnManager::loadPreset(const juce::String& _name, MidiLearnPreset& _preset)
	{
		const auto file = getPresetFile(_name);
		return _preset.loadFromFile(file);
	}

	bool MidiLearnManager::deletePreset(const juce::String& _name)
	{
		const auto file = getPresetFile(_name);
		if (!file.existsAsFile())
			return false;

		return file.deleteFile();
	}

	bool MidiLearnManager::renamePreset(const juce::String& _oldName, const juce::String& _newName)
	{
		const auto oldFile = getPresetFile(_oldName);
		const auto newFile = getPresetFile(_newName);

		if (!oldFile.existsAsFile() || newFile.existsAsFile())
			return false;

		return oldFile.moveFileTo(newFile);
	}

	juce::String MidiLearnManager::createUniquePresetName(const juce::String& _baseName) const
	{
		auto name = _baseName;
		int counter = 1;

		while (getPresetFile(name).existsAsFile())
		{
			name = _baseName + " (" + juce::String(counter) + ")";
			++counter;
		}

		return name;
	}
}

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../jucePluginLib/parameterbinding.h"

namespace juce
{
	class Component;
}

class AudioPluginAudioProcessor;

class PluginEditorState
{
public:
	struct Skin
	{
		std::string displayName;
		std::string jsonFilename;
		std::string folder;

		bool operator == (const Skin& _other) const
		{
			return displayName == _other.displayName && jsonFilename == _other.jsonFilename && folder == _other.folder;
		}
	};

	explicit PluginEditorState(AudioPluginAudioProcessor& _processor);

	void exportCurrentSkin() const;
	Skin readSkinFromConfig() const;
	void writeSkinToConfig(const Skin& _skin) const;

	float getRootScale() const { return m_rootScale; }

	int getWidth() const;
	int getHeight() const;

	const Skin& getCurrentSkin() { return m_currentSkin; }
	static const std::vector<Skin>& getIncludedSkins();

	void openMenu();

	std::function<void(int)> evSetGuiScale;
	std::function<void(juce::Component*)> evSkinLoaded;

	juce::Component* getUiRoot() const;

	void disableBindings();
	void enableBindings();

private:
	void loadSkin(const Skin& _skin);
	void setGuiScale(int _scale) const;

	AudioPluginAudioProcessor& m_processor;

	pluginLib::ParameterBinding m_parameterBinding;

	std::unique_ptr<juce::Component> m_virusEditor;
	Skin m_currentSkin;
	float m_rootScale = 1.0f;
};

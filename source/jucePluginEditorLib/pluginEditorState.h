#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../jucePluginLib/parameterbinding.h"

namespace genericUI
{
	class Editor;
}

namespace juce
{
	class Component;
}

class AudioPluginAudioProcessor;

namespace jucePluginEditorLib
{
	class Processor;

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

		explicit PluginEditorState(Processor& _processor, pluginLib::Controller& _controller, std::vector<Skin> _includedSkins);
		virtual ~PluginEditorState() = default;

		PluginEditorState(PluginEditorState&&) = delete;
		PluginEditorState(const PluginEditorState&) = delete;

		PluginEditorState& operator = (PluginEditorState&&) = delete;
		PluginEditorState& operator = (const PluginEditorState&) = delete;

		void exportCurrentSkin() const;
		Skin readSkinFromConfig() const;
		void writeSkinToConfig(const Skin& _skin) const;

		float getRootScale() const { return m_rootScale; }

		int getWidth() const;
		int getHeight() const;

		const Skin& getCurrentSkin() { return m_currentSkin; }
		const std::vector<Skin>& getIncludedSkins();

		void openMenu();

		std::function<void(int)> evSetGuiScale;
		std::function<void(juce::Component*)> evSkinLoaded;

		juce::Component* getUiRoot() const;

		void disableBindings();
		void enableBindings();

		void loadDefaultSkin();

		virtual void initContextMenu(juce::PopupMenu& _menu) {}

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

	protected:
		virtual genericUI::Editor* createEditor(const Skin& _skin, std::function<void()> _openMenuCallback) = 0;

		Processor& m_processor;
		pluginLib::ParameterBinding m_parameterBinding;

	private:
		void loadSkin(const Skin& _skin);
		void setGuiScale(int _scale) const;

		genericUI::Editor* getEditor() const;

		std::unique_ptr<juce::Component> m_editor;
		Skin m_currentSkin;
		float m_rootScale = 1.0f;
		std::vector<Skin> m_includedSkins;
		std::vector<uint8_t> m_instanceConfig;
	};
}

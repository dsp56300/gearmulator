#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "client/serverList.h"

#include "skin.h"

namespace pluginLib
{
	class Controller;
}

namespace juceRmlUi
{
	class Menu;
}

namespace Rml
{
	class Event;
}

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class Editor;
	class Processor;

	class PluginEditorState
	{
	public:
		explicit PluginEditorState(Processor& _processor, pluginLib::Controller& _controller, std::vector<Skin> _includedSkins);
		virtual ~PluginEditorState();

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

		bool resizeEditor(int _width, int _height) const;

		const Skin& getCurrentSkin() { return m_currentSkin; }
		const std::vector<Skin>& getIncludedSkins();

		static std::string createSkinDisplayName(std::string _filename);

		virtual void openMenu(const Rml::Event& _event);

		std::function<void(int)> evSetGuiScale;
		std::function<void(juce::Component*)> evSkinLoaded;

		juce::Component* getUiRoot() const;

		void loadDefaultSkin();

		virtual void initContextMenu(juceRmlUi::Menu& _menu) {}
		virtual bool initAdvancedContextMenu(juceRmlUi::Menu& _menu, bool _enabled) { return false; }

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

		std::string getSkinFolder() const;

		static std::string getSkinFolder(const std::string& _processorDataFolder);
		static std::string getSkinSubfolder(const Skin& _skin, const std::string& _folder);

		bool hasSkin() const
		{
			return m_currentSkin.isValid();
		}

		Editor* getEditor() const;

	protected:
		virtual Editor* createEditor(const Skin& _skin) = 0;

		Processor& m_processor;

	private:
		bool loadSkin(const Skin& _skin, uint32_t _fallbackIndex = 0);
		void setGuiScale(int _scale) const;
		std::string exportSkinToFolder(const Skin& _skin, const std::string& _folder) const;

		std::unique_ptr<Editor> m_editor;
		Skin m_currentSkin;
		float m_rootScale = 1.0f;
		std::vector<Skin> m_includedSkins;
		std::vector<uint8_t> m_instanceConfig;
		std::string m_skinFolderName;
		std::unique_ptr<bridgeClient::ServerList> m_remoteServerList;
	};
}

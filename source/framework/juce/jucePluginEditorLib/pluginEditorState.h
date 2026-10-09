#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
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
	struct WindowSize
	{
		int width = 0;
		int height = 0;

		bool isValid() const { return width > 0 && height > 0; }
	};

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

		Skin readSkinFromConfig() const;
		void writeSkinToConfig(const Skin& _skin) const;

		float getRootScale() const { return m_rootScale; }

		int getWidth() const;
		int getHeight() const;

		bool resizeEditor(int _width, int _height) const;

		const Skin& getCurrentSkin() const { return m_currentSkin; }
		const std::vector<Skin>& getIncludedSkins();

		static std::string createSkinDisplayName(std::string _filename);

		virtual void openMenu(const Rml::Event& _event);

		std::function<void(int)> evSetGuiScale;
		std::function<void(juce::Component*)> evSkinLoaded;
		std::function<void()> evSkinSizeChanged;
		std::function<void()> evFreeWindowSizeRestored;

		// The window size of a skin in the free window mode, per instance and kept with the plugin state. 0 if
		// there is none yet. restoreFreeWindowSize() is for a size that comes from a state, the window follows it.
		const WindowSize& getFreeWindowSize() const { return m_freeWindowSize; }
		void setFreeWindowSize(const WindowSize& _size) { m_freeWindowSize = _size; }
		void restoreFreeWindowSize(const WindowSize& _size);

		juce::Component* getUiRoot() const;

		void loadDefaultSkin();

		virtual void initContextMenu(juceRmlUi::Menu& _menu) {}

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

		std::string getSkinFolder() const;

		static std::string getSkinFolder(const std::string& _processorDataFolder);
		static std::string getSkinSubfolder(const Skin& _skin, const std::string& _folder);

		bool hasSkin() const
		{
			return m_currentSkin.isValid();
		}

		bool loadSkin(const Skin& _skin, uint32_t _fallbackIndex = 0);
		std::string exportSkinToFolder(const Skin& _skin, const std::string& _folder) const;

		Editor* getEditor() const;

		void enableDspBridge(bool _enable);
		bridgeClient::ServerList* getRemoteServerList() const { return m_remoteServerList.get(); }

	protected:
		virtual Editor* createEditor(const Skin& _skin) = 0;

		Processor& m_processor;

	private:
		void setGuiScale(int _scale) const;

		std::unique_ptr<Editor> m_editor;
		Skin m_currentSkin;
		float m_rootScale = 1.0f;
		std::vector<Skin> m_includedSkins;
		std::vector<uint8_t> m_instanceConfig;
		WindowSize m_freeWindowSize;
		std::string m_skinFolderName;
		std::unique_ptr<bridgeClient::ServerList> m_remoteServerList;
	};

	// The editor state of a product that only needs to create its editor. A product that overrides more, such as
	// initContextMenu, derives from PluginEditorState instead. TProcessor is the type the editor's constructor takes
	template<class TEditor, class TProcessor = Processor>
	class PluginEditorStateT final : public PluginEditorState
	{
	public:
		PluginEditorStateT(TProcessor& _processor, std::vector<Skin> _includedSkins)
			: PluginEditorState(_processor, _processor.getController(), std::move(_includedSkins))
		{
			loadDefaultSkin();
		}

	private:
		Editor* createEditor(const Skin& _skin) override
		{
			return new TEditor(static_cast<TProcessor&>(m_processor), _skin);
		}
	};
}

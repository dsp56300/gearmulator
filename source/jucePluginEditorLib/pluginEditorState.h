#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "client/serverList.h"

#include "skin.h"

#include "jucePluginLib/parameterbinding.h"

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

		virtual void openMenu(const juce::MouseEvent* _event);

		std::function<void(int)> evSetGuiScale;
		std::function<void(juce::Component*)> evSkinLoaded;

		juce::Component* getUiRoot() const;

		void disableBindings();
		void enableBindings();

		void loadDefaultSkin();

		virtual void initContextMenu(juce::PopupMenu& _menu) {}
		virtual bool initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled) { return false; }

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

		std::string getSkinFolder() const;

		bool hasSkin() const
		{
			return m_currentSkin.isValid();
		}
	protected:
		virtual Editor* createEditor(const Skin& _skin) = 0;

		Processor& m_processor;
		pluginLib::ParameterBinding m_parameterBinding;

		Editor* getEditor() const;

	private:
		bool loadSkin(const Skin& _skin, uint32_t _fallbackIndex = 0);
		void setGuiScale(int _scale) const;

		std::unique_ptr<juce::Component> m_editor;
		Skin m_currentSkin;
		float m_rootScale = 1.0f;
		std::vector<Skin> m_includedSkins;
		std::vector<uint8_t> m_instanceConfig;
		std::string m_skinFolderName;
		bridgeClient::ServerList m_remoteServerList;
	};
}

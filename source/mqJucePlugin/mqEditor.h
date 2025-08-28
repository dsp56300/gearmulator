#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

namespace jucePluginEditorLib
{
	class FocusedParameter;
	class Processor;
}

namespace mqJucePlugin
{
	class mqPartSelect;
	class Controller;
	class FrontPanel;
	class PatchManager;

	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		void create() override;

		jucePluginEditorLib::patchManager::PatchManager* createPatchManager(Rml::Element* _parent) override;

		void initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions&) override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		Controller& getMqController() const { return m_controller; }

		mqPartSelect* getPartSelect() const
		{
			return m_partSelect.get();
		}

	private:
		void savePreset(const pluginLib::FileType& _type);

		void onBtSave(const Rml::Event& _event);
		void onBtPresetPrev() const;
		void onBtPresetNext() const;

		Controller& m_controller;

		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<mqPartSelect> m_partSelect;
		Rml::Element* m_btPlayModeMulti = nullptr;
		Rml::Element* m_btSave = nullptr;
		Rml::Element* m_btPresetPrev = nullptr;
		Rml::Element* m_btPresetNext = nullptr;

		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;
	};
}

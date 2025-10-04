#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

#include "jucePluginLib/patchdb/patch.h"

namespace jucePluginEditorLib
{
	class FocusedParameter;
	class MidiPorts;
	class Processor;
}

namespace jeJucePlugin
{
	class PartSelect;
	class JeLcd;
	class Controller;

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

		Controller& geJeController() const { return m_controller; }

	private:
		Controller& m_controller;

		std::unique_ptr<jucePluginEditorLib::MidiPorts> m_midiPorts;

		baseLib::EventListener<uint8_t> onPartChanged;

		std::array<std::string, 4> m_activePatchNames;

		std::unique_ptr<JeLcd> m_lcd;
		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;
		std::unique_ptr<PartSelect> m_partSelect;
	};
}

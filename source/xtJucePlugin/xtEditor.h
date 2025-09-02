#pragma once

#include "xtParts.h"

#include "jucePluginEditorLib/pluginEditor.h"

#include "baseLib/event.h"

namespace jucePluginEditorLib
{
	class MidiPorts;
	class FocusedParameter;
	class Processor;
}

namespace pluginLib
{
	class ParameterBinding;
}

namespace xtJucePlugin
{
	class WaveEditor;
	class FocusedParameter;
	class FrontPanel;
	class PatchManager;
	class XtLcd;
	class Controller;
	class Arp;

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

		Controller& getXtController() const { return m_controller; }

		XtLcd* getLcd() const;
		Parts& getParts() const;

		void setCurrentPart(uint8_t _part) override;

		const WaveEditor* getWaveEditor() const { return m_waveEditor; }

	private:
		void changeWave(int _step) const;

		Controller& m_controller;

		std::unique_ptr<FocusedParameter> m_focusedParameter;
		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<Parts> m_parts;
		std::unique_ptr<jucePluginEditorLib::MidiPorts> m_midiPorts;
		std::unique_ptr<Arp> m_arp;

		WaveEditor* m_waveEditor = nullptr;

		baseLib::EventListener<bool> m_playModeChangeListener;

		Rml::Element* m_btMultiMode = nullptr;
		Rml::Element* m_ledMultiMode = nullptr;
	};
}

#pragma once

#include "xtParts.h"

#include "jucePluginEditorLib/pluginEditor.h"

#include "baseLib/event.h"

namespace jucePluginEditorLib
{
	class Processor;
}

namespace pluginLib
{
	class ParameterBinding;
}

namespace xtJucePlugin
{
	class WaveEditor;
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

		void initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions&) override;

		Controller& getXtController() const { return m_controller; }

		XtLcd* getLcd() const;
		void onFocusedParameterChanged(const std::string& _name, const std::string& _value) override;
		Parts& getParts() const;

		const WaveEditor* getWaveEditor() const { return m_waveEditor; }

	private:
		void onCurrentPartChanged(uint8_t _part) override;
		void changeWave(int _step) const;

		Controller& m_controller;

		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<Parts> m_parts;
		std::unique_ptr<Arp> m_arp;

		WaveEditor* m_waveEditor = nullptr;

		baseLib::EventListener<bool> m_playModeChangeListener;

		Rml::Element* m_btMultiMode = nullptr;
		Rml::Element* m_ledMultiMode = nullptr;
	};
}

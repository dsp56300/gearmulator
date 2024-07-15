#pragma once

#include "xtParts.h"

#include "jucePluginEditorLib/pluginEditor.h"

#include "jucePluginLib/event.h"

class XtLcd;
class Controller;

namespace jucePluginEditorLib
{
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

	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		Controller& getXtController() const { return m_controller; }

		XtLcd* getLcd() const;
		Parts& getParts() const;

		genericUI::Button<juce::DrawableButton>* createJuceComponent(genericUI::Button<juce::DrawableButton>*, genericUI::UiObject& _object, const std::string& _name, juce::DrawableButton::ButtonStyle) override;
		genericUI::Button<juce::TextButton>* createJuceComponent(genericUI::Button<juce::TextButton>*, genericUI::UiObject& _object) override;
		juce::Component* createJuceComponent(juce::Component*, genericUI::UiObject& _object) override;

		void setCurrentPart(uint8_t _part) override;
	private:
		void mouseEnter(const juce::MouseEvent& _event) override;
		void changeWave(int _step) const;

		Controller& m_controller;
		pluginLib::ParameterBinding& m_parameterBinding;

		std::unique_ptr<FocusedParameter> m_focusedParameter;
		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<Parts> m_parts;
		WaveEditor* m_waveEditor = nullptr;

		pluginLib::EventListener<bool> m_playModeChangeListener;

		juce::Button* m_btMultiMode = nullptr;
		juce::Button* m_ledMultiMode = nullptr;
		juce::Button* m_btSave = nullptr;
	};
}

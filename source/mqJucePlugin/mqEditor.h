#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

namespace jucePluginEditorLib
{
	class FocusedParameter;
	class Processor;
}

namespace pluginLib
{
	class ParameterBinding;
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
		Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		Controller& getMqController() const { return m_controller; }

		genericUI::Button<juce::DrawableButton>* createJuceComponent(genericUI::Button<juce::DrawableButton>*, genericUI::UiObject& _object, const std::string& _name, juce::DrawableButton::ButtonStyle) override;

		mqPartSelect* getPartSelect() const
		{
			return m_partSelect.get();
		}

	private:
		void mouseEnter(const juce::MouseEvent& _event) override;

		void savePreset(jucePluginEditorLib::FileType _type);

		void onBtSave();
		void onBtPresetPrev();
		void onBtPresetNext();

		Controller& m_controller;

		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<mqPartSelect> m_partSelect;
		juce::Button* m_btPlayModeMulti = nullptr;
		juce::Button* m_btSave = nullptr;
		juce::Button* m_btPresetPrev = nullptr;
		juce::Button* m_btPresetNext = nullptr;

		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;
	};
}

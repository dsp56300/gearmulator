#pragma once

#include "../jucePluginEditorLib/pluginEditor.h"
#include "../jucePluginEditorLib/focusedParameter.h"

#include "mqFrontPanel.h"
#include "mqPatchBrowser.h"
#include "mqPartSelect.h"

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
	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename);
		~Editor() override;
		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;

	private:
		void mouseEnter(const juce::MouseEvent& _event) override;

		void savePreset(FileType _type);

		void onBtSave();
		void onBtPresetPrev();
		void onBtPresetNext();

		Controller& m_controller;

		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<PatchBrowser> m_patchBrowser;
		std::unique_ptr<mqPartSelect> m_partSelect;
		juce::Button* m_btPlayModeMulti = nullptr;
		juce::Button* m_btSave = nullptr;
		juce::Button* m_btPresetPrev = nullptr;
		juce::Button* m_btPresetNext = nullptr;

		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;
	};
}

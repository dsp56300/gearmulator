#pragma once

#include "../jucePluginEditorLib/pluginEditor.h"

#include "mqFrontPanel.h"
#include "mqPatchBrowser.h"

namespace jucePluginEditorLib
{
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
		Controller& m_controller;

		std::unique_ptr<FrontPanel> m_frontPanel;
		std::unique_ptr<PatchBrowser> m_patchBrowser;
		juce::Button* m_btPlayModeMulti = nullptr;
	};
}

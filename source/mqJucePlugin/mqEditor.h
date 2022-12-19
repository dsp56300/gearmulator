#pragma once

#include "../jucePluginEditorLib/pluginEditor.h"

#include "mqFrontPanel.h"

namespace pluginLib
{
	class ParameterBinding;
	class Processor;
}

namespace mqJucePlugin
{
	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		Editor(pluginLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename);
		~Editor() override;
		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;

	private:
		std::unique_ptr<FrontPanel> m_frontPanel;
	};
}

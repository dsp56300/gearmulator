#pragma once

#include "../jucePluginEditorLib/pluginEditor.h"

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
		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;
	};
}

#pragma once

#include "../juceUiLib/editor.h"

namespace pluginLib
{
	class Processor;
	class ParameterBinding;
}

namespace jucePluginEditorLib
{
	class Editor : public genericUI::Editor, genericUI::EditorInterface
	{
	public:
		Editor(pluginLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder);

		virtual const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) = 0;

	private:
		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		int getParameterIndexByName(const std::string& _name) override;
		bool bindParameter(juce::Button& _target, int _parameterIndex) override;
		bool bindParameter(juce::ComboBox& _target, int _parameterIndex) override;
		bool bindParameter(juce::Slider& _target, int _parameterIndex) override;
		juce::Value* getParameterValue(int _parameterIndex, uint8_t _part) override;

		pluginLib::Processor& m_processor;
		pluginLib::ParameterBinding& m_binding;

		const std::string m_skinFolder;

		std::map<std::string, std::vector<char>> m_fileCache;
	};
}

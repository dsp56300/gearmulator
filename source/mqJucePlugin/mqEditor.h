#pragma once

#include "../juceUiLib/editor.h"

namespace pluginLib
{
	class ParameterBinding;
	class Processor;
}

namespace mqJucePlugin
{
	class Editor final : public genericUI::EditorInterface, public genericUI::Editor
	{
	public:
		Editor(pluginLib::Processor& _processor, pluginLib::ParameterBinding& _binding);
		static const char* findNamedResourceByFilename(const std::string& _filename, uint32_t& _size);
		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		int getParameterIndexByName(const std::string& _name) override;
		juce::Value* getParameterValue(int _parameterIndex) override;
		bool bindParameter(juce::Slider& _target, int _parameterIndex) override;
		bool bindParameter(juce::Button& _target, int _parameterIndex) override;
		bool bindParameter(juce::ComboBox& _target, int _parameterIndex) override;

	private:
		pluginLib::Processor& m_processor;
		pluginLib::ParameterBinding& m_parameterBinding;
	};
}

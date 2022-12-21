#pragma once

namespace genericUI
{
	class EditorInterface
	{
	public:
		virtual ~EditorInterface() = default;

		virtual const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) = 0;

		virtual int getParameterIndexByName(const std::string& _name) = 0;
		virtual juce::Value* getParameterValue(int _parameterIndex, uint8_t _part) = 0;

		virtual bool bindParameter(juce::Slider& _target, int _parameterIndex) = 0;
		virtual bool bindParameter(juce::Button& _target, int _parameterIndex) = 0;
		virtual bool bindParameter(juce::ComboBox& _target, int _parameterIndex) = 0;
	};
}

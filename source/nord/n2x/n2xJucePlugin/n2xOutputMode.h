#pragma once
#include "n2xFocusedParameter.h"
#include "jucePluginLib/types.h"

namespace juce
{
	class ComboBox;
}

namespace n2xJucePlugin
{
	class Editor;

	class OutputMode
	{
	public:
		OutputMode(const Editor& _editor);

	private:
		void setOutModeAB(uint8_t _mode);
		void setOutModeCD(uint8_t _mode);
		void onOutModeChanged(pluginLib::ParamValue _paramValue);

		juce::ComboBox* m_outAB;
		juce::ComboBox* m_outCD;
		pluginLib::Parameter* const m_parameter;

		pluginLib::ParameterListener m_onOutputModeChanged;
	};
}

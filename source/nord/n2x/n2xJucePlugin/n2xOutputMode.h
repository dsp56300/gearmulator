#pragma once

#include "jucePluginLib/types.h"
#include "jucePluginLib/parameterlistener.h"

namespace pluginLib
{
	class Parameter;
}

namespace juceRmlUi
{
	class ElemComboBox;
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

		juceRmlUi::ElemComboBox* m_outAB;
		juceRmlUi::ElemComboBox* m_outCD;
		pluginLib::Parameter* const m_parameter;

		pluginLib::ParameterListener m_onOutputModeChanged;
	};
}

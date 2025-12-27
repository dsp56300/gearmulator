#pragma once

#include "jucePluginEditorLib/focusedParameter.h"

namespace jeJucePlugin
{
	class JeLcd;
	class Editor;
}

namespace jeJucePlugin
{
	class JeFocusedParameter : public jucePluginEditorLib::FocusedParameter
	{
	public:
		JeFocusedParameter(Editor& _editor, JeLcd& _lcd);

		void updateParameter(const std::string& _name, const std::string& _value) override;

	private:
		Editor& m_editor;
		JeLcd& m_lcd;
	};
}

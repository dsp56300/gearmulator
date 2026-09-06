#pragma once

#include "jucePluginEditorLib/focusedParameter.h"

namespace n2xJucePlugin
{
	class Editor;

	class FocusedParameter : public jucePluginEditorLib::FocusedParameter
	{
	public:
		explicit FocusedParameter(const Editor& _editor);
		void updateParameter(const std::string& _name, const std::string& _value) override;

	private:
		const Editor& m_editor;
	};
}

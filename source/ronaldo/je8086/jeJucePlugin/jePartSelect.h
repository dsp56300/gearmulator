#pragma once

#include <array>

#include "jeFocusedParameter.h"

namespace juceRmlUi
{
	class ElemButton;
}

namespace jeJucePlugin
{
	class Editor;

	class PartSelect
	{
	public:
		explicit PartSelect(Editor& _editor);

	private:
		void onPartSelectChanged() const;
		void updateButtonStates() const;
		void onClick(Rml::Event& _event, size_t _part) const;

		Editor& m_editor;
		std::array<juceRmlUi::ElemButton*, 2> m_partButtons{};

		pluginLib::Parameter* m_paramPanelSelect = nullptr;
		pluginLib::ParameterListener m_partSelectListener;
	};
}

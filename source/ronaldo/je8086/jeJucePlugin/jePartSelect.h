#pragma once

#include <array>

#include "jeFocusedParameter.h"
#include "jeTypes.h"

namespace jucePluginEditorLib
{
	class PartButton;
}

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

		PartSelect(const PartSelect&) = delete;
		PartSelect& operator=(const PartSelect&) = delete;
		PartSelect(PartSelect&&) = delete;
		PartSelect& operator=(PartSelect&&) = delete;

		~PartSelect();

	private:
		void onPartSelectChanged() const;
		void updateButtonStates() const;
		void onClick(Rml::Event& _event, size_t _part) const;

		void onPatchNameChanged(PatchType _patch, const std::string& _name) const;

		Editor& m_editor;

		std::array<std::vector<juceRmlUi::ElemButton*>, 2> m_partButtons;
		std::array<std::vector<Rml::Element*>, 2> m_patchNames;
		std::array<std::vector<std::unique_ptr<jucePluginEditorLib::PartButton*>>, 2> m_partButton;

		pluginLib::Parameter* m_paramPanelSelect = nullptr;
		pluginLib::ParameterListener m_partSelectListener;
		baseLib::EventListener<PatchType, std::string> m_onPatchNameChanged;
	};
}

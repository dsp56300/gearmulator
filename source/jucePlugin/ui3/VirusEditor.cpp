#include "VirusEditor.h"

#include "BinaryData.h"
#include "../VirusController.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(VirusParameterBinding& _binding, Virus::Controller& _controller) : Editor(std::string(BinaryData::VirusC_json, BinaryData::VirusC_jsonSize), _binding, _controller),
		m_parts(*this),
		m_tabs(*this)
	{
		m_presetName = findComponentT<juce::Label>("PatchName");

		getController().onProgramChange = [this] { onProgramChange(); };
	}

	void VirusEditor::onProgramChange()
	{
		m_parts.onProgramChange();

		m_presetName->setText(getController().getCurrentPartPresetName(getController().getCurrentPart()), juce::dontSendNotification);
	}
}

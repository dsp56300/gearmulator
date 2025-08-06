#pragma once

#include <vector>

#include "baseLib/event.h"

#include "virusLib/frontpanelState.h"

#include "juce_events/juce_events.h"	// juce:Timer

namespace juceRmlUi
{
	class ElemButton;
}

namespace juceRmlUi
{
	class ElemKnob;
}

namespace Rml
{
	class Element;
}

namespace genericVirusUI
{
	class PartButton;
	class VirusEditor;

	class Parts : juce::Timer
	{
	public:
		explicit Parts(VirusEditor& _editor);
		~Parts() override;

		void onProgramChange() const;
		void onPlayModeChanged() const;
		void onCurrentPartChanged() const;

	private:
		void selectPart(size_t _part) const;
		void selectPartMidiChannel(size_t _part) const;
		void selectPrevPreset(size_t _part) const;
		void selectNextPreset(size_t _part) const;

		void updatePresetNames() const;
		void updateSelectedPart() const;
		void updateAll() const;
		void updateSingleOrMultiMode() const;

		void timerCallback() override;

		VirusEditor& m_editor;

		std::vector<juceRmlUi::ElemButton*> m_partSelect;
		std::vector<Rml::Element*> m_presetPrev;
		std::vector<Rml::Element*> m_presetNext;

		std::vector<juceRmlUi::ElemKnob*> m_partVolume;
		std::vector<juceRmlUi::ElemKnob*> m_partPan;
		std::vector<Rml::Element*> m_partActive;

		std::vector<std::unique_ptr<PartButton>> m_presetName;

		virusLib::FrontpanelState m_frontpanelState;
		baseLib::EventListener<virusLib::FrontpanelState> m_onFrontpanelStateChanged;
	};
}

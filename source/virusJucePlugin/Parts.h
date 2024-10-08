#pragma once

#include <vector>

#include "jucePluginLib/event.h"
#include "juceUiLib/button.h"
#include "virusLib/frontpanelState.h"

namespace genericVirusUI
{
	class PartButton;
	class VirusEditor;

	class PartMouseListener : public juce::MouseListener
	{
	public:
		explicit PartMouseListener(const int _part, const std::function<void(const juce::MouseEvent&, int)>& _callback) : m_part(_part), m_callback(_callback)
		{
		}

		void mouseDrag(const juce::MouseEvent& _event) override
		{
			m_callback(_event, m_part);
		}

	private:
		int m_part;
		std::function<void(const juce::MouseEvent&, int)> m_callback;
	};

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

		std::vector<genericUI::Button<juce::DrawableButton>*> m_partSelect;
		std::vector<juce::Button*> m_presetPrev;
		std::vector<juce::Button*> m_presetNext;

		std::vector<juce::Slider*> m_partVolume;
		std::vector<juce::Slider*> m_partPan;
		std::vector<juce::Component*> m_partActive;

		std::vector<PartButton*> m_presetName;

		virusLib::FrontpanelState m_frontpanelState;
		pluginLib::EventListener<virusLib::FrontpanelState> m_onFrontpanelStateChanged;
	};
}

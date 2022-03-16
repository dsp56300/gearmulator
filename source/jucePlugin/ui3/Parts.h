#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace genericVirusUI
{
	class VirusEditor;

	class Parts
	{
	public:
		explicit Parts(VirusEditor& _editor);
		virtual ~Parts();

		void onProgramChange() const;
		void onPlayModeChanged() const;
		void onCurrentPartChanged() const;

	private:
		void selectPart(size_t _part) const;
		void selectPrevPreset(size_t _part) const;
		void selectNextPreset(size_t _part) const;
		void selectPreset(size_t _part) const;

		void updatePresetNames() const;
		void updateSelectedPart() const;
		void updateAll() const;
		void updateSingleOrMultiMode() const;

		VirusEditor& m_editor;

		std::vector<juce::Button*> m_partSelect;
		std::vector<juce::Button*> m_presetPrev;
		std::vector<juce::Button*> m_presetNext;

		std::vector<juce::Slider*> m_partVolume;
		std::vector<juce::Slider*> m_partPan;

		std::vector<juce::TextButton*> m_presetName;
	};
}

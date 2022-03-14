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

		void onProgramChange();

	private:
		void selectPart(size_t _part);
		void selectPrevPreset(size_t _part);
		void selectNextPreset(size_t _part);
		void selectPreset(size_t _part);

		void updatePresetNames();
		void updateSelectedPart();
		void updateAll();
		void updateSingleOrMultiMode();

		VirusEditor& m_editor;

		std::vector<juce::Button*> m_partSelect;
		std::vector<juce::Button*> m_presetPrev;
		std::vector<juce::Button*> m_presetNext;

		std::vector<juce::Slider*> m_partVolume;
		std::vector<juce::Slider*> m_partPan;

		std::vector<juce::TextButton*> m_presetName;
	};
}

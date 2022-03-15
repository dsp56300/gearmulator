#pragma once

namespace juce
{
	class Component;
}

namespace genericVirusUI
{
	class VirusEditor;

	class FxPage
	{
	public:
		explicit FxPage(VirusEditor& _editor);

	private:
		void updateReverbDelay() const;

		VirusEditor& m_editor;
		juce::Component* m_reverbContainer = nullptr;
		juce::Component* m_delayContainer = nullptr;
	};
}

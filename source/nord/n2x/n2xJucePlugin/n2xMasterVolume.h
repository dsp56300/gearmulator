#pragma once

namespace juce
{
	class Slider;
}

namespace n2xJucePlugin
{
	class Editor;

	class MasterVolume
	{
	public:
		explicit MasterVolume(const Editor& _editor);

	private:
		const Editor& m_editor;

		juce::Slider* m_volume;
	};
}

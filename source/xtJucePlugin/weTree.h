#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	class WaveEditor;
	class Editor;

	class Tree : public juce::TreeView
	{
	public:
		explicit Tree(WaveEditor& _editor);
		~Tree() override;

		WaveEditor& getWaveEditor() const { return m_editor; }

		void parentHierarchyChanged() override;
	private:
		WaveEditor& m_editor;

		void paint(juce::Graphics& _g) override;
	};
}

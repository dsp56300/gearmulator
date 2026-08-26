#pragma once

#include "juceRmlUi/rmlElemTree.h"
#include "juceRmlUi/rmlInstancers.h"

namespace juce
{
	class Graphics;
}

namespace xtJucePlugin
{
	class WaveEditor;
	class Editor;

	class Tree : public juceRmlUi::ElemTree
	{
	public:
		explicit Tree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor, bool _withCanvas);
		~Tree() override;

		WaveEditor& getWaveEditor() const { return m_editor; }

		virtual Rml::ElementPtr createChild(const std::string& _tag) = 0;

	private:
		WaveEditor& m_editor;

		juceRmlUi::CallbackElemInstancer m_instancer;

		void paint(juce::Graphics& _g);
	};
}

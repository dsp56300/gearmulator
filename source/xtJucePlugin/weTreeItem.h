#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	class TreeItem : public juce::TreeViewItem
	{
	public:
		void setText(const std::string& _text);

	protected:
		void paintItem(juce::Graphics& _g, int _width, int _height) override;

	private:
		bool mightContainSubItems() override
		{
			return true;
		}

		std::string m_text;
	};
}

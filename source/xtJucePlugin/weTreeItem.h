#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	class TreeItem : public juce::TreeViewItem
	{
	public:
		void setText(const std::string& _text);

	protected:
		virtual juce::Colour getTextColor(const juce::Colour _colour) { return _colour; }

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

		bool paintInBold() const;

		void setPaintRootItemInBold(const bool _enabled)
		{
			m_paintRootItemInBold = _enabled;
		}

	private:
		bool mightContainSubItems() override
		{
			return true;
		}

		std::string m_text;
		bool m_paintRootItemInBold = true;
	};
}

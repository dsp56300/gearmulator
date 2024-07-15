#include "weTreeItem.h"

#include "juceUiLib/treeViewStyle.h"

namespace xtJucePlugin
{
	void TreeItem::setText(const std::string& _text)
	{
		if(_text == m_text)
			return;

		m_text = _text;
		repaintItem();
	}

	void TreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		const auto* style = dynamic_cast<const genericUI::TreeViewStyle*>(&getOwnerView()->getLookAndFeel());

		_g.setColour(style ? style->getColor() : juce::Colour(0xffffffff));

		bool haveFont = false;
		if(style)
		{
			if (auto f = style->getFont())
			{
				f->setBold(paintInBold());
				_g.setFont(*f);
				haveFont = true;
			}
		}
		if(!haveFont)
		{
			auto fnt = _g.getCurrentFont();
			fnt.setBold(paintInBold());
			_g.setFont(fnt);
		}

		
		const juce::String t = juce::String::fromUTF8(m_text.c_str());
		_g.drawText(t, 0, 0, _width, _height, style ? style->getAlign() : juce::Justification(juce::Justification::centredLeft));

		TreeViewItem::paintItem(_g, _width, _height);
	}

	bool TreeItem::paintInBold() const
	{
		return m_paintRootItemInBold && getParentItem() == getOwnerView()->getRootItem();
	}

}

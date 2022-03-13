#include "uiObjectStyle.h"

#include "editor.h"
#include "uiObject.h"

namespace genericUI
{
	void UiObjectStyle::setTileSize(int _w, int _h)
	{
		m_tileSizeX = _w;
		m_tileSizeY = _h;
	}

	void UiObjectStyle::setDrawable(juce::Drawable* _drawable)
	{
		m_drawable = _drawable;
	}

	void UiObjectStyle::setTextHeight(int _height)
	{
		m_textHeight = _height;
	}

	void UiObjectStyle::apply(Editor& _editor, const UiObject& _object)
	{
		const auto x = _object.getPropertyInt("tileSizeX");
		const auto y = _object.getPropertyInt("tileSizeY");

		setTileSize(x,y);

		const auto tex = _object.getProperty("texture");
		if(!tex.empty())
			setDrawable(_editor.getImageDrawable(tex));

		setTextHeight(_object.getPropertyInt("textHeight"));
	}

	void UiObjectStyle::applyFontProperties(juce::Font& _font) const
	{
		if(m_textHeight)
			_font.setHeight(static_cast<float>(m_textHeight));
	}
}

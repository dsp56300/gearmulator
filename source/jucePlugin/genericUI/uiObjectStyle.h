#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace genericUI
{
	class Editor;
	class UiObject;

	class UiObjectStyle : public juce::LookAndFeel_V4
	{
	public:
		void setTileSize(int _w, int _h);
		void setDrawable(juce::Drawable* _drawable);
		void setTextHeight(int _height);

		virtual void apply(Editor& _editor, const UiObject& _object);

	protected:
		void applyFontProperties(juce::Font& _font) const;

		int m_tileSizeX = 0;
		int m_tileSizeY = 0;
		juce::Drawable* m_drawable = nullptr;
		int m_textHeight = 0;
	};
}

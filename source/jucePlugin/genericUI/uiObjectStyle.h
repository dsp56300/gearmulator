#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace genericUI
{
	class Editor;
	class UiObject;

	class UiObjectStyle : public juce::LookAndFeel_V4
	{
	public:
		UiObjectStyle();

		void setTileSize(int _w, int _h);
		void setDrawable(juce::Drawable* _drawable);
		void setTextHeight(int _height);

		virtual void apply(Editor& _editor, const UiObject& _object);

	protected:
		juce::Font getComboBoxFont(juce::ComboBox&) override;
		juce::Font getLabelFont(juce::Label&) override;
		juce::Font getPopupMenuFont() override;
		juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

		void applyFontProperties(juce::Font& _font) const;

		int m_tileSizeX = 0;
		int m_tileSizeY = 0;
		juce::Drawable* m_drawable = nullptr;
		int m_textHeight = 0;
		std::string m_text;
		juce::Colour m_color = juce::Colour(0xffffffff);
		juce::Justification m_align = 0;
	};
}

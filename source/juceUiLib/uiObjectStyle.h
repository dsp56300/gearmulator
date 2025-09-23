#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include <optional>

namespace genericUI
{
	class Editor;
	class UiObject;

	class UiObjectStyle : public juce::LookAndFeel_V4
	{
	public:
		UiObjectStyle(Editor& _editor);

		void setTileSize(int _w, int _h);
		void setDrawable(juce::Drawable* _drawable);
		void setTextHeight(int _height);

		virtual void apply(Editor& _editor, const UiObject& _object);

		const auto& getColor() const { return m_color; }
		const auto& getBackgroundColor() const { return m_bgColor; }
		const auto& getSelectedItemBackgroundColor() const { return m_selectedItemBgColor; }
		const auto& getOutlineColor() const { return m_outlineColor; }
		const auto& getAlign() const { return m_align; }

		std::optional<juce::Font> getFont() const;
		const auto& getFontName() const { return m_fontName; }
		const auto& getFontFile() const { return m_fontFile; }
		int getTextHeight() const { return m_textHeight; }

		bool getAntialiasing() const { return m_antialiasing; }

		bool getBold() const { return m_bold; }
		bool getItalic() const { return m_italic; }

		static bool parseColor(juce::Colour& _color, const std::string& _colorString);

		void drawLabel(juce::Graphics&, juce::Label&) override;
		void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

		const auto& getHitAreaOffset() const { return m_hitAreaOffset; }

		int getComboOffsetL() const { return m_offsetL; }
		int getComboOffsetT() const { return m_offsetT; }
		int getComboOffsetR() const { return m_offsetR; }
		int getComboOffsetB() const { return m_offsetB; }	

	protected:
		juce::Font getComboBoxFont(juce::ComboBox&) override;
		juce::Font getLabelFont(juce::Label&) override;
		juce::Font getPopupMenuFont() override;
		juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
		bool shouldPopupMenuScaleWithTargetComponent (const juce::PopupMenu::Options&) override;
		juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox&, juce::Label&) override;

		void applyFontProperties(juce::Font& _font) const;

		static void applyColorIfNotZero(juce::Component& _target, int _id, const juce::Colour& _col);

		Editor& m_editor;

		int m_tileSizeX = 0;
		int m_tileSizeY = 0;

		juce::Drawable* m_drawable = nullptr;

		std::string m_fontFile;
		std::string m_fontName;
		int m_textHeight = 0;
		std::string m_text;
		bool m_antialiasing = true;

		juce::Colour m_color = juce::Colour(0xffffffff);
		juce::Colour m_bgColor = juce::Colour(0);
		juce::Colour m_linesColor = juce::Colour(0);
		juce::Colour m_selectedItemBgColor = juce::Colour(0);
		juce::Colour m_dragAndDropIndicatorColor = juce::Colour(0);
		juce::Colour m_outlineColor = juce::Colour(0);

		juce::Justification m_align = 0;

		bool m_bold = false;
		bool m_italic = false;
		int m_offsetL = 0;
		int m_offsetT = 0;
		int m_offsetR = 0;
		int m_offsetB = 0;

		std::string m_url;

		juce::Rectangle<int> m_hitAreaOffset;	// only supported for buttons atm
	};
}

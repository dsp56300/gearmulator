#pragma once

#include <cstdint>
#include <vector>
#include <array>

#include "juceRmlUi/rmlElemCanvas.h"

#include "juce_graphics/juce_graphics.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	struct LcdConfig
	{
		LcdConfig(const float _pixelSpacing)
			: pixelSpacingAdjust(_pixelSpacing)
			, pixelsPerCharW(5)
			, pixelsPerCharH(8)
			, pixelSpacingW(0.05f * pixelSpacingAdjust)
			, pixelSizeW(0.6f)
			, charSpacingW(0.4f)
			, charSizeW(static_cast<float>(pixelsPerCharW) * pixelSizeW + pixelSpacingW * static_cast<float>(pixelsPerCharW - 1))
			, pixelStrideW(pixelSizeW + pixelSpacingW)
			, charStrideW(charSizeW + charSpacingW)
			, pixelSpacingH(0.05f * pixelSpacingAdjust)
			, pixelSizeH(0.65f)
			, charSpacingH(0.4f)
			, charSizeH(static_cast<float>(pixelsPerCharH) * pixelSizeH + pixelSpacingH * static_cast<float>(pixelsPerCharH - 1))
			, pixelStrideH(pixelSizeH + pixelSpacingH)
			, charStrideH(charSizeH + charSpacingH)
		{
		}

		const float pixelSpacingAdjust;

		const int pixelsPerCharW;
		const int pixelsPerCharH;

		const float pixelSpacingW;
		const float pixelSizeW;
		const float charSpacingW;

		const float charSizeW;
		const float pixelStrideW;
		const float charStrideW;

		const float pixelSpacingH;
		const float pixelSizeH;
		const float charSpacingH;

		const float charSizeH;
		const float pixelStrideH;
		const float charStrideH;
	};

	class Lcd : public juce::MultiTimer
	{
	public:
		explicit Lcd(Rml::Element* _parent, uint32_t _numCharsX, uint32_t _numCharsY, float _pixelSpacing = 3.0f);	// 1.0f = 100% as on the hardware, but it looks better on screen if it's a bit more
		virtual ~Lcd();

		void setText(const std::vector<uint8_t> &_text);
		void setCgRam(const std::array<uint8_t, 64> &_data);

		// NOTE: nothing calls this yet, so the blink timer and the underline renderer below have
		// never run. The 88emu boards are the only owners of an hwLib::Hd44780 and they draw
		// through their own DisplaySnapshot, not this class. Drive it from a panel that has cursor
		// state and both paths come alive at once - expect them to be untested until then.
		//
		// Cursor state (HD44780 semantics). _col/_row are character coordinates;
		// pass -1/-1 (or any out-of-range pair) to indicate the cursor is not
		// currently over a visible cell.
		void setCursor(bool _displayOn, bool _cursorOn, bool _blinking, int _col, int _row);

		Rml::Element* getElement() const;

	protected:
		// Left click or context menu on the display. The default shows the
		// subclass's getOverrideText() for a few seconds; override to put the
		// click to a different use, such as renaming the current patch.
		virtual void onClicked();

	private:
		// Timer IDs for juce::MultiTimer.
		enum
		{
			kTimerOverrideText = 0,
			kTimerBlink        = 1,
		};

		void setSize(uint32_t _width, uint32_t _height);
		void paint(const juce::Image& _image, juce::Graphics& _g);
		juce::Path createPath(uint8_t _character) const;

		void repaint() const;

		// Whether the cursor cell is one the canvas actually draws. setCursor() uses it to
		// decide whether the blink timer is worth running, so it has to agree with paint() -
		// checking only for a non-negative position there left the timer repainting the whole
		// LCD twice a second for a cursor parked past the end of the window.
		bool isCursorInWindow() const;

		virtual bool getOverrideText(std::vector<std::string>& _lines) { return false; }
		virtual bool getOverrideText(std::vector<std::vector<uint8_t>>& _lines);
		virtual const uint8_t* getCharacterData(uint8_t _character) const = 0;

		void timerCallback(int _timerId) override;

		LcdConfig m_config;

		std::array<juce::Path, 256> m_characterPaths;

		float m_scaleW = 0;
		float m_scaleH = 0;

		uint32_t m_numCharsX = 0;
		uint32_t m_numCharsY = 0;

		uint32_t m_width = 0;
		uint32_t m_height = 0;

		std::vector<uint8_t> m_overrideText;
		std::vector<uint8_t> m_text;

		std::array<std::array<uint8_t, 8>, 8> m_cgData{{{0}}};

		uint32_t m_charBgColor = 0xff000000;
		uint32_t m_charColor = 0xff000000;

		// Cursor state. Defaults match the HD44780 power-on state: display on,
		// cursor off, no blink, no visible cell.
		bool m_displayOn       = true;
		bool m_cursorOn        = false;
		bool m_cursorBlinking  = false;
		int  m_cursorCol       = -1;
		int  m_cursorRow       = -1;
		bool m_blinkPhase      = false;

		juceRmlUi::ElemCanvas* m_canvas;
	};
}

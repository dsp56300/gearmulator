#include "lcd.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

// LCD simulation

namespace
{
}

namespace jucePluginEditorLib
{
	Lcd::Lcd(Rml::Element* _parent, const uint32_t _numCharsX, const uint32_t _numCharsY, const float _pixelSpacing)
	: m_config(_pixelSpacing)
	, m_numCharsX(_numCharsX)
	, m_numCharsY(_numCharsY)
	{
		m_canvas = juceRmlUi::ElemCanvas::create(_parent);

		m_canvas->setClearEveryFrame(true);

		m_canvas->setRepaintGraphicsCallback([this](const juce::Image& _image, juce::Graphics& _graphics)
		{
			paint(_image, _graphics);
		});

		{
			const std::string bgColor = juceRmlUi::Element::getProperty(_parent, "lcdBackgroundColor", std::string());
			if (bgColor.size() == 6)
				m_charBgColor = strtol(bgColor.c_str(), nullptr, 16) | 0xff000000;
		}

		{
			const std::string color = juceRmlUi::Element::getProperty(_parent, "lcdTextColor", std::string());
			if (color.size() == 6)
				m_charColor = strtol(color.c_str(), nullptr, 16) | 0xff000000;
		}

		m_text.resize(_numCharsX * _numCharsY, 255);	// block character
		m_overrideText.resize(_numCharsX * _numCharsY, 0);

		m_cgData.fill({});

		juceRmlUi::EventListener::AddClick(m_canvas, [this]
		{
			onClicked();
		});

		juceRmlUi::EventListener::Add(m_canvas, Rml::EventId::Mousedown, [this](Rml::Event& _e)
		{
			if (!juceRmlUi::helper::isContextMenu(_e))
				return;

			_e.StopPropagation();
			onClicked();
		});
	}

	Lcd::~Lcd() = default;

	void Lcd::setText(const std::vector<uint8_t>& _text)
	{
		if (m_text == _text)
			return;

		m_text = _text;

		repaint();
	}

	void Lcd::setCgRam(const std::array<uint8_t, 64>& _data)
	{
		for (uint8_t i=0; i<static_cast<uint8_t>(m_cgData.size()); ++i)
		{
			std::array<uint8_t, 8> c{};
			memcpy(c.data(), &_data[i*8], 8);

			if (c != m_cgData[i])
			{
				m_cgData[i] = c;
				m_characterPaths[i] = createPath(i);
			}
		}

		repaint();
	}

	Rml::Element* Lcd::getElement() const
	{
		return m_canvas;
	}

	void Lcd::setSize(const uint32_t _width, const uint32_t _height)
	{
		if (m_width == _width && m_height == _height)
			return;

		m_width = _width;
		m_height = _height;

		m_scaleW = static_cast<float>(m_width) / (static_cast<float>(m_numCharsX) * m_config.charSizeW + m_config.charSpacingW * (static_cast<float>(m_numCharsX) - 1));
	    m_scaleH = static_cast<float>(m_height) / (static_cast<float>(m_numCharsY) * m_config.charSizeH + m_config.charSpacingH * (static_cast<float>(m_numCharsY) - 1));

		for (uint32_t i=0; i<m_characterPaths.size(); ++i)
			m_characterPaths[i] = createPath(static_cast<uint8_t>(i));
	}

	void Lcd::paint(const juce::Image& _image, juce::Graphics& _g)
	{
		setSize(static_cast<uint32_t>(_image.getWidth()), static_cast<uint32_t>(_image.getHeight()));

		const auto& text = m_overrideText[0] ? m_overrideText : m_text;

		uint32_t charIdx = 0;

		for (uint32_t y=0; y < m_numCharsY; ++y)
		{
			const auto ty = static_cast<float>(y) * m_config.charStrideH * m_scaleH;

			for (uint32_t x = 0; x < m_numCharsX; ++x, ++charIdx)
			{
				const auto tx = static_cast<float>(x) * m_config.charStrideW * m_scaleW;

				const auto t = juce::AffineTransform::translation(tx, ty);

				const auto c = text[charIdx];
				const auto& p = m_characterPaths[c];

				if (m_charBgColor)
				{
					_g.setColour(juce::Colour(m_charBgColor));
					_g.fillPath(m_characterPaths[255], t);
				}

				// HD44780: when display is off the panel is blank — skip the
				// glyph entirely (background still renders so the LCD doesn't
				// disappear visually).
				if (!m_displayOn)
					continue;

				const bool isCursorCell = (static_cast<int>(x) == m_cursorCol && static_cast<int>(y) == m_cursorRow);

				// Blink: HD44780 alternates the cell between the character and a
				// solid block. Skip the glyph during the "block" phase; the
				// block is drawn explicitly below.
				if (isCursorCell && m_cursorBlinking && m_blinkPhase)
				{
					_g.setColour(juce::Colour(m_charColor));
					_g.fillPath(m_characterPaths[255], t);
				}
				else
				{
					_g.setColour(juce::Colour(m_charColor));
					_g.fillPath(p, t);
				}
			}
		}

		// Cursor underline. Drawn last so it sits on top of the glyph row.
		// Rendered as 5 individual dots in the bottom pixel row to match the
		// LCD's pixel grid — a solid bar would betray the dot-matrix look.
		if (m_displayOn && m_cursorOn && m_cursorCol >= 0 && m_cursorRow >= 0 &&
			m_cursorCol < static_cast<int>(m_numCharsX) && m_cursorRow < static_cast<int>(m_numCharsY))
		{
			const auto tx = static_cast<float>(m_cursorCol) * m_config.charStrideW * m_scaleW;
			const auto ty = static_cast<float>(m_cursorRow) * m_config.charStrideH * m_scaleH;

			const auto pxW = m_config.pixelSizeW * m_scaleW;
			const auto pxH = m_config.pixelSizeH * m_scaleH;
			const auto pxStrideW = m_config.pixelStrideW * m_scaleW;
			const auto pxY = ty + 7.0f * m_config.pixelStrideH * m_scaleH;

			_g.setColour(juce::Colour(m_charColor));
			for (int px = 0; px <= 4; ++px)
			{
				const auto pxX = tx + static_cast<float>(px) * pxStrideW;
				_g.fillRect(pxX, pxY, pxW, pxH);
			}
		}
	}

	juce::Path Lcd::createPath(const uint8_t _character) const
	{
		const auto* data = _character < m_cgData.size() ? m_cgData[_character].data() : getCharacterData(_character);

		juce::Path path;

		const auto h = m_config.pixelSizeH * m_scaleH;
		const auto w = m_config.pixelSizeW * m_scaleW;

		for (auto y=0; y<8; ++y)
		{
			const auto y0 = static_cast<float>(y) * m_config.pixelStrideH * m_scaleH;

			for (auto x=0; x<=4; ++x)
			{
				const auto bit = 4-x;

				const auto set = data[y] & (1<<bit);

				if(!set)
					continue;

				const auto x0 = static_cast<float>(x) * m_config.pixelStrideW * m_scaleW;

				path.addRectangle(x0, y0, w, h);
			}
		}

		return path;
	}

	void Lcd::onClicked()
	{
		if(isTimerRunning(kTimerOverrideText))
			return;

		std::vector<std::vector<uint8_t>> lines;
		getOverrideText(lines);

		for (auto& c : m_overrideText)
			c = ' ';

		for(size_t y=0; y<std::min(lines.size(), static_cast<size_t>(m_numCharsY)); ++y)
		{
			memcpy(&m_overrideText[m_numCharsX*y], lines[y].data(), std::min(lines[y].size(), static_cast<size_t>(m_numCharsX)));
		}

		startTimer(kTimerOverrideText, 3000);
		repaint();
	}

	void Lcd::setCursor(const bool _displayOn, const bool _cursorOn, const bool _blinking, const int _col, const int _row)
	{
		if (m_displayOn == _displayOn && m_cursorOn == _cursorOn &&
			m_cursorBlinking == _blinking && m_cursorCol == _col && m_cursorRow == _row)
			return;

		m_displayOn = _displayOn;
		m_cursorOn = _cursorOn;
		m_cursorBlinking = _blinking;
		m_cursorCol = _col;
		m_cursorRow = _row;

		// Drive the blink animation only while it would be visible; saves the
		// repaint loop when the cursor is off-screen or not blinking.
		const bool wantBlink = _displayOn && _blinking && _col >= 0 && _row >= 0;
		if (wantBlink)
		{
			if (!isTimerRunning(kTimerBlink))
				startTimer(kTimerBlink, 500);
		}
		else
		{
			stopTimer(kTimerBlink);
			m_blinkPhase = false;
		}

		repaint();
	}

	void Lcd::repaint() const
	{
		m_canvas->repaint();
	}

	bool Lcd::getOverrideText(std::vector<std::vector<uint8_t>>& _lines)
	{
		std::vector<std::string> strLines;

		if (!getOverrideText(strLines))
			return false;

		_lines.reserve(2);
		_lines.emplace_back(m_numCharsX, ' ');
		_lines.emplace_back(m_numCharsX, ' ');

		for (size_t i=0; i<std::min(strLines.size(), static_cast<size_t>(2)); ++i)
			memcpy(_lines[i].data(), strLines[i].c_str(), std::min(strLines[i].size(), static_cast<size_t>(m_numCharsX)));

		return true;
	}

	void Lcd::timerCallback(const int _timerId)
	{
		switch (_timerId)
		{
		case kTimerOverrideText:
			stopTimer(kTimerOverrideText);
			m_overrideText[0] = 0;
			repaint();
			break;
		case kTimerBlink:
			m_blinkPhase = !m_blinkPhase;
			repaint();
			break;
		default:
			break;
		}
	}
}
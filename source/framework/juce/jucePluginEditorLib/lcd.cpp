#include "lcd.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

// LCD simulation

namespace
{
	constexpr int g_pixelsPerCharW = 5;
	constexpr int g_pixelsPerCharH = 8;

	constexpr float g_pixelSpacingAdjust = 3.0f;	// 1.0f = 100% as on the hardware, but it looks better on screen if it's a bit more

	constexpr float g_pixelSpacingW = 0.05f * g_pixelSpacingAdjust;
	constexpr float g_pixelSizeW = 0.6f;
	constexpr float g_charSpacingW = 0.4f;

	constexpr float g_charSizeW = g_pixelsPerCharW * g_pixelSizeW + g_pixelSpacingW * (g_pixelsPerCharW - 1);
	constexpr float g_pixelStrideW = g_pixelSizeW + g_pixelSpacingW;
	constexpr float g_charStrideW = g_charSizeW + g_charSpacingW;

	constexpr float g_pixelSpacingH = 0.05f * g_pixelSpacingAdjust;
	constexpr float g_pixelSizeH = 0.65f;
	constexpr float g_charSpacingH = 0.4f;

	constexpr float g_charSizeH = g_pixelsPerCharH * g_pixelSizeH + g_pixelSpacingH * (g_pixelsPerCharH - 1);
	constexpr float g_pixelStrideH = g_pixelSizeH + g_pixelSpacingH;
	constexpr float g_charStrideH = g_charSizeH + g_charSpacingH;
}

namespace jucePluginEditorLib
{
	Lcd::Lcd(Rml::Element* _parent, uint32_t _numCharsX, uint32_t _numCharsY)
	: m_numCharsX(_numCharsX)
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

		m_scaleW = static_cast<float>(m_width) / (static_cast<float>(m_numCharsX) * g_charSizeW + g_charSpacingW * (static_cast<float>(m_numCharsX) - 1));
	    m_scaleH = static_cast<float>(m_height) / (static_cast<float>(m_numCharsY) * g_charSizeH + g_charSpacingH * (static_cast<float>(m_numCharsY) - 1));

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
			const auto ty = static_cast<float>(y) * g_charStrideH * m_scaleH;

			for (uint32_t x = 0; x < m_numCharsX; ++x, ++charIdx)
			{
				const auto tx = static_cast<float>(x) * g_charStrideW * m_scaleW;

				const auto t = juce::AffineTransform::translation(tx, ty);

				const auto c = text[charIdx];
				const auto& p = m_characterPaths[c];

				if (m_charBgColor)
				{
					_g.setColour(juce::Colour(m_charBgColor));
					_g.fillPath(m_characterPaths[255], t);
				}
				_g.setColour(juce::Colour(m_charColor));
				_g.fillPath(p, t);
			}
		}
	}

	juce::Path Lcd::createPath(const uint8_t _character) const
	{
		const auto* data = _character < m_cgData.size() ? m_cgData[_character].data() : getCharacterData(_character);

		juce::Path path;

		const auto h = g_pixelSizeH * m_scaleH;
		const auto w = g_pixelSizeW * m_scaleW;

		for (auto y=0; y<8; ++y)
		{
			const auto y0 = static_cast<float>(y) * g_pixelStrideH * m_scaleH;

			for (auto x=0; x<=4; ++x)
			{
				const auto bit = 4-x;

				const auto set = data[y] & (1<<bit);

				if(!set)
					continue;

				const auto x0 = static_cast<float>(x) * g_pixelStrideW * m_scaleW;

				path.addRectangle(x0, y0, w, h);
			}
		}

		return path;
	}

	void Lcd::onClicked()
	{
		if(isTimerRunning())
			return;

		std::vector<std::vector<uint8_t>> lines;
		getOverrideText(lines);

		for (auto& c : m_overrideText)
			c = ' ';

		for(size_t y=0; y<std::min(lines.size(), static_cast<size_t>(m_numCharsY)); ++y)
		{
			memcpy(&m_overrideText[m_numCharsX*y], lines[y].data(), std::min(lines[y].size(), static_cast<size_t>(m_numCharsX)));
		}

		startTimer(3000);
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

	void Lcd::timerCallback()
	{
		stopTimer();
		m_overrideText[0] = 0;
		repaint();
	}
}
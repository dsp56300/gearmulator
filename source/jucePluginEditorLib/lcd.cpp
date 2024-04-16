#include "lcd.h"

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
Lcd::Lcd(juce::Component& _parent, const uint32_t _width, const uint32_t _height)
	: Button("LCDButton"),
	m_parent(_parent),
    m_scaleW(static_cast<float>(_parent.getWidth()) / (static_cast<float>(_width) * g_charSizeW + g_charSpacingW * (static_cast<float>(_width) - 1))),
    m_scaleH(static_cast<float>(_parent.getHeight()) / (static_cast<float>(_height) * g_charSizeH + g_charSpacingH * (static_cast<float>(_height) - 1))),
	m_width(_width),
	m_height(_height)
{
	setSize(_parent.getWidth(), _parent.getHeight());

	{
		const std::string bgColor = _parent.getProperties().getWithDefault("lcdBackgroundColor", juce::String()).toString().toStdString();
		if (bgColor.size() == 6)
			m_charBgColor = strtol(bgColor.c_str(), nullptr, 16) | 0xff000000;
	}

	{
		const std::string color = _parent.getProperties().getWithDefault("lcdTextColor", juce::String()).toString().toStdString();
		if (color.size() == 6)
			m_charColor = strtol(color.c_str(), nullptr, 16) | 0xff000000;
	}

	m_text.resize(_width * _height, 255);	// block character
	m_overrideText.resize(_width * _height, 0);

	m_cgData.fill({0});

	onClick = [&]()
	{
		onClicked();
	};

	setEnabled(true);
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

void Lcd::postConstruct()
{
	for (uint32_t i=16; i<256; ++i)
		m_characterPaths[i] = createPath(static_cast<uint8_t>(i));

	m_parent.addAndMakeVisible(this);
}

void Lcd::paint(juce::Graphics& _g)
{
	const auto& text = m_overrideText[0] ? m_overrideText : m_text;

	uint32_t charIdx = 0;

	for (uint32_t y=0; y < m_height; ++y)
	{
		const auto ty = static_cast<float>(y) * g_charStrideH * m_scaleH;

		for (uint32_t x = 0; x < m_width; ++x, ++charIdx)
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

	for(size_t y=0; y<std::min(lines.size(), static_cast<size_t>(m_height)); ++y)
	{
		memcpy(&m_overrideText[m_width*y], lines[y].data(), std::min(lines[y].size(), static_cast<size_t>(m_width)));
	}

	startTimer(3000);
}

void Lcd::timerCallback()
{
	stopTimer();
	m_overrideText[0] = 0;
	repaint();
}
}
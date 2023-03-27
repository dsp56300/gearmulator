#include "mqLcd.h"

#include "../mqLib/lcdfonts.h"

constexpr int g_pixelsPerCharW = 5;
constexpr int g_pixelsPerCharH = 8;
constexpr int g_numCharsW = 20;
constexpr int g_numCharsH = 2;
constexpr int g_numPixelsW = g_numCharsW * g_pixelsPerCharW + g_numCharsW - 1;
constexpr int g_numPixelsH = g_numCharsH * g_pixelsPerCharH + g_numCharsH - 1;

constexpr float g_pixelBorder = 0.1f;

MqLcd::MqLcd(Component& _parent) : m_pixelW(static_cast<float>(_parent.getWidth()) / static_cast<float>(g_numPixelsW)), m_pixelH(static_cast<float>(_parent.getHeight()) / static_cast<float>(g_numPixelsH))
{
	setSize(_parent.getWidth(), _parent.getHeight());

	_parent.addAndMakeVisible(this);

	for (uint32_t i=16; i<256; ++i)
	{
		m_characterPaths[i] = createPath(static_cast<uint8_t>(i));
	}

	const std::string color = _parent.getProperties().getWithDefault("lcdBackgroundColor", juce::String()).toString().toStdString();

	if (color.size() == 6)
	{
		m_charBgColor = strtol(color.c_str(), nullptr, 16) | 0xff000000;
	}

	m_text.fill(255);	// block character
	m_cgData.fill({0});
}

MqLcd::~MqLcd() = default;

void MqLcd::setText(const std::array<uint8_t, 40>& _text)
{
	if (m_text == _text)
		return;

	m_text = _text;

	repaint();
}

void MqLcd::setCgRam(std::array<uint8_t, 64>& _data)
{
	for (auto i=0; i<m_cgData.size(); ++i)
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

void MqLcd::paint(juce::Graphics& _g)
{
	uint32_t charIdx = 0;

	for (auto y=0; y<2; ++y)
	{
		const auto ty = m_pixelH * (g_pixelsPerCharH + 1) * static_cast<float>(y);

		for (auto x = 0; x < 20; ++x, ++charIdx)
		{
			const auto tx = m_pixelW * (g_pixelsPerCharW + 1) * static_cast<float>(x);

			const auto t = juce::AffineTransform::translation(tx, ty);

			const auto c = m_text[charIdx];
			const auto& p = m_characterPaths[c];

			if (m_charBgColor)
			{
				_g.setColour(juce::Colour(m_charBgColor));
				_g.fillPath(m_characterPaths[255], t);
			}
			_g.setColour(juce::Colour(0xff000000));
			_g.fillPath(p, t);
		}
	}
}

juce::Path MqLcd::createPath(const uint8_t _character) const
{
	const auto* data = _character < m_cgData.size() ? m_cgData[_character].data() : mqLib::getCharacterData(_character);

	juce::Path path;

	const auto h = m_pixelH;
	const auto w = m_pixelW;

	const auto borderX = g_pixelBorder * w;
	const auto borderY = g_pixelBorder * h;
	const auto borderW = borderX * 2.0f;
	const auto borderH = borderY * 2.0f;
	const auto wWithBorder = w - borderW;
	const auto hWithBorder = h - borderH;

	for (auto y=0; y<8; ++y)
	{
		const auto y0 = static_cast<float>(y) * m_pixelH;

		for (auto x=0; x<=4; ++x)
		{
			const auto bit = 4-x;

			const auto set = data[y] & (1<<bit);

			if(!set)
				continue;

			const auto x0 = static_cast<float>(x) * m_pixelW;

			path.addRectangle(x0 + borderX, y0 + borderY, wWithBorder, hWithBorder);
		}
	}

	return path;
}

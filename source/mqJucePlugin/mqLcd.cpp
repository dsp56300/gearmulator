#include "mqLcd.h"

#include "version.h"
#include "../mqLib/lcdfonts.h"

// EW20290GLW display simulation

constexpr int g_pixelsPerCharW = 5;
constexpr int g_pixelsPerCharH = 8;

constexpr int g_numCharsW = 20;
constexpr int g_numCharsH = 2;

constexpr float g_pixelSpacingAdjust = 3.0f;	// 1.0f = 100% as on the hardware, but it looks better on screen if its a bit more

constexpr float g_pixelSpacingW = 0.05f * g_pixelSpacingAdjust;
constexpr float g_pixelSizeW = 0.6f;
constexpr float g_charSpacingW = 0.4f;

constexpr float g_charSizeW = g_pixelsPerCharW * g_pixelSizeW + g_pixelSpacingW * (g_pixelsPerCharW - 1);
constexpr float g_sizeW = g_numCharsW * g_charSizeW + g_charSpacingW * (g_numCharsW - 1);
constexpr float g_pixelStrideW = g_pixelSizeW + g_pixelSpacingW;
constexpr float g_charStrideW = g_charSizeW + g_charSpacingW;

constexpr float g_pixelSpacingH = 0.05f * g_pixelSpacingAdjust;
constexpr float g_pixelSizeH = 0.65f;
constexpr float g_charSpacingH = 0.4f;

constexpr float g_charSizeH = g_pixelsPerCharH * g_pixelSizeH + g_pixelSpacingH * (g_pixelsPerCharH - 1);
constexpr float g_sizeH = g_numCharsH * g_charSizeH + g_charSpacingH * (g_numCharsH - 1);
constexpr float g_pixelStrideH = g_pixelSizeH + g_pixelSpacingH;
constexpr float g_charStrideH = g_charSizeH + g_charSpacingH;

constexpr int g_numPixelsW = g_numCharsW * g_pixelsPerCharW + g_numCharsW - 1;
constexpr int g_numPixelsH = g_numCharsH * g_pixelsPerCharH + g_numCharsH - 1;

constexpr float g_pixelBorder = 0.1f;

MqLcd::MqLcd(Component& _parent) : juce::Button("mqLCDButton"), m_scaleW(static_cast<float>(_parent.getWidth()) / g_sizeW), m_scaleH(static_cast<float>(_parent.getHeight()) / g_sizeH)
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

	onClick = [&]()
	{
		onClicked();
	};

	setEnabled(true);
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
	const auto& text = m_overrideText[0] ? m_overrideText : m_text;

	uint32_t charIdx = 0;

	for (auto y=0; y<2; ++y)
	{
		const auto ty = static_cast<float>(y) * g_charStrideH * m_scaleH;

		for (auto x = 0; x < 20; ++x, ++charIdx)
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
			_g.setColour(juce::Colour(0xff000000));
			_g.fillPath(p, t);
		}
	}
}

juce::Path MqLcd::createPath(const uint8_t _character) const
{
	const auto* data = _character < m_cgData.size() ? m_cgData[_character].data() : mqLib::getCharacterData(_character);

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

void MqLcd::onClicked()
{
	if(isTimerRunning())
		return;

	const std::string lineA(std::string("Vavra v") + g_pluginVersionString);
	const std::string lineB = __DATE__ " " __TIME__;

	m_overrideText.fill(' ');

	memcpy(m_overrideText.data(), lineA.c_str(), std::min(std::size(lineA), static_cast<size_t>(20)));
	memcpy(&m_overrideText[20], lineB.c_str(), std::min(std::size(lineB), static_cast<size_t>(20)));

	startTimer(3000);
}

void MqLcd::timerCallback()
{
	stopTimer();
	m_overrideText[0] = 0;
	repaint();
}

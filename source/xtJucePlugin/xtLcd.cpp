#include "xtLcd.h"

#include "xtController.h"

#include "jucePluginLib/pluginVersion.h"

#include "hardwareLib/lcdfonts.h"

namespace
{
	constexpr char g_defaultTextSingle[] =
		"  Play Sound EEEEE |  Mode   |Main Vol. "
		"  0123456789012345 |  Sound  |   VVV    ";

	constexpr char g_defaultTextMulti[] =
		"  Play Multi  001  |  Mode   |Main Vol. "
		"  0123456789012345 |  Multi  |   VVV   Z";

	static_assert(std::size(g_defaultTextSingle) == 80 + 1);
	static_assert(std::size(g_defaultTextMulti) == 80 + 1);
}

namespace xtJucePlugin
{
	// 40*2 LCD simulation

	XtLcd::XtLcd(Component& _parent, Controller& _controller) : Lcd(_parent, 40, 2), m_controller(_controller)
	{
		postConstruct();

		m_currentText.fill(' ');
	}

	XtLcd::~XtLcd() = default;

	void XtLcd::refresh()
	{
		setText(m_currentText);
	}

	void XtLcd::setText(const std::array<uint8_t, 80>& _text)
	{
		m_currentText = _text;

		std::vector<uint8_t> text{_text.begin(), _text.end()};

		if(m_controller.isMultiMode())
			text.back() = '1' + m_controller.getCurrentPart();

		if(!m_paramName.empty() && !m_paramValue.empty())
		{
			const auto param = '[' + m_paramName + " = " + m_paramValue + ']';
			if(param.size() <= 40)
			{
				memcpy(text.data() + 40 - param.size(), param.c_str(), param.size());
			}
		}

		Lcd::setText(text);
	}

	bool XtLcd::getOverrideText(std::vector<std::vector<uint8_t>>& _lines)
	{
		std::string lineA(std::string("Xenia v") + pluginLib::Version::getVersionString());
		std::string lineB = pluginLib::Version::getVersionDateTime();

		constexpr char lineAright[] = "From TUS";
		constexpr char lineBright[] = "with <3";

		while(lineA.size() < 40 - std::size(lineAright) + 1)
			lineA.push_back(' ');
		lineA += lineAright;

		while(lineB.size() < 40 - std::size(lineBright) + 1)
			lineB.push_back(' ');
		lineB += lineBright;

		_lines = 
		{
			std::vector<uint8_t>(lineA.begin(), lineA.end()),
			std::vector<uint8_t>(lineB.begin(), lineB.end())
		};

		return true;
	}

	const uint8_t* XtLcd::getCharacterData(const uint8_t _character) const
	{
		return hwLib::getCharacterData(_character);
	}

	void XtLcd::setCurrentParameter(const std::string& _name, const std::string& _value)
	{
		m_paramName = _name;
		m_paramValue = _value;

		setText(m_currentText);
	}
}
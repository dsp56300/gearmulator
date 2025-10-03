#include "jeLcd.h"

#include "jeController.h"
#include "jeEditor.h"
#include "hardwareLib/lcdfonts.h"

#include "jucePluginLib/version.h"

namespace jeJucePlugin
{
	JeLcd::JeLcd(Editor& _editor, Rml::Element* _parent) : Lcd(_parent, 16, 2)
	{
		auto& sr = _editor.geJeController().getSysexRemote();

		m_onLcdCgDataChanged.set(sr.evLcdCgDataChanged, [this](const std::array<uint8_t, 64>& _data)
		{
			setCgRam(_data);
		});
		m_onLcdDdDataChanged.set(sr.evLcdDdDataChanged, [this](const std::array<char, 40>& _data)
		{
			std::vector<uint8_t> text;
			text.reserve(16 * 2);
			for (size_t i=0; i<16; ++i)
				text.push_back(static_cast<uint8_t>(_data[i]));
			for (size_t i=0; i<16; ++i)
				text.push_back(static_cast<uint8_t>(_data[i + 20]));
			setText(text);
		});
	}

	const uint8_t* JeLcd::getCharacterData(uint8_t _character) const
	{
		return hwLib::getCharacterData(_character);
	}

	bool JeLcd::getOverrideText(std::vector<std::string>& _lines)
	{
		_lines.emplace_back(std::string("JE-8086 v") + g_pluginVersionString);
		_lines.emplace_back("From TUS with <3");

		return true;
	}
}

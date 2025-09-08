#include "jeLcd.h"

#include "hardwareLib/lcdfonts.h"

#include "jucePluginLib/version.h"

namespace jeJucePlugin
{
	JeLcd::JeLcd(Rml::Element* _parent): Lcd(_parent, 16, 2)
	{
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

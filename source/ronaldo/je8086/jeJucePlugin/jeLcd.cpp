#include "jeLcd.h"

#include "jeController.h"
#include "jeEditor.h"
#include "jePluginProcessor.h"
#include "hardwareLib/lcdfonts.h"

#include "jucePluginLib/version.h"
#include "juceRmlUi/rmlInplaceEditor.h"

namespace jeJucePlugin
{
	JeLcd::JeLcd(Editor& _editor, Rml::Element* _parent) : Lcd(_parent, 16, 2), m_editor(_editor)
	{
		auto& sr = _editor.getJeController().getSysexRemote();

		if (!_editor.getJeProcessor().getSelectedRom().isValid())
		{
			setText({
				"No ROM no sound!    "
				"Error 524288    "
			});
		}
		else
		{
			setText({
				"                    "
				"                "
			});
		}

		m_onLcdCgDataChanged.set(sr.evLcdCgDataChanged, [this](const std::array<uint8_t, 64>& _data)
		{
			setCgRam(_data);
		});
		m_onLcdDdDataChanged.set(sr.evLcdDdDataChanged, [this](const std::array<char, 40>& _data)
		{
			setText(_data);
		});

		juceRmlUi::EventListener::Add(getElement(), Rml::EventId::Dblclick, [this](Rml::Event& _event)
		{
			_event.StopPropagation();
			new juceRmlUi::InplaceEditor(_event.GetTargetElement(), m_editor.getJeController().getPatchName(PatchType::Performance),
			[this](const std::string& _newName)
			{
				m_editor.getJeController().changePatchName(PatchType::Performance, _newName);
			});
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

	void JeLcd::setText(const std::array<char, 40>& _data)
	{
		m_lcdText = _data;

		std::vector<uint8_t> text;
		text.reserve(16 * 2);

		for (size_t i=0; i<16; ++i)
			text.push_back(static_cast<uint8_t>(_data[i]));
		for (size_t i=0; i<16; ++i)
			text.push_back(static_cast<uint8_t>(_data[i + 20]));

		Lcd::setText(text);
	}

	void JeLcd::setParameterDisplay(const std::string& _name, const std::string& _value)
	{
		if (_name.empty() || _value.empty())
		{
			setText(m_lcdText);
			return;
		}

		std::vector<uint8_t> text;
		text.reserve(16 * 2);

		for (size_t i=0; i<16; ++i)
			text.push_back(i < _name.size() ? _name[i] : ' ');

		const auto valueSize = static_cast<int>(_value.size());

		int valueIndex = valueSize - 16;

		for (size_t i=0; i<16; ++i, ++valueIndex)
		{
			if (valueIndex >= 0 && valueIndex < valueSize)
				text.push_back(_value[valueIndex]);
			else
				text.push_back(' ');
		}

		Lcd::setText(text);
	}
}

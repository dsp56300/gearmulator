#include "jeLcd.h"

#include <cstring> // for strncmp

#include "jeController.h"
#include "jeEditor.h"
#include "jePluginProcessor.h"
#include "hardwareLib/lcdfonts.h"
#include "jeLib/je8086devices.h"

#include "jucePluginLib/version.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceRmlUi/rmlElemButton.h"

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
				"Booting...          "
				"From TUS with <3 "
			});
		}

		m_onLcdCgDataChanged.set(sr.evLcdCgDataChanged, [this](const std::array<uint8_t, 64>& _data)
		{
			setCgRam(_data);
		});
		m_onLcdDdDataChanged.set(sr.evLcdDdDataChanged, [this](const std::array<char, 40>& _data)
		{
			setText(_data);

			if (!m_btPerfPatch)
				return;

			// this is not stored anywhere so we have to parse the LCD text
			if (m_btPerfPatch->isChecked())
			{
				if (strncmp(_data.data(), "PERFORM", 7) == 0)
					m_btPerfPatch->setChecked(false);
			}
			else
			{
				if (strncmp(_data.data(), "PATCH", 5) == 0)
					m_btPerfPatch->setChecked(true);
			}
		});

		juceRmlUi::EventListener::Add(getElement(), Rml::EventId::Dblclick, [this](Rml::Event& _event)
		{
			_event.StopPropagation();
			renamePerformance();
		});

		m_btPerfPatch = m_editor.findChild<juceRmlUi::ElemButton>("btPerfPatch", false);

		if (m_btPerfPatch)
		{
			juceRmlUi::EventListener::Add(m_btPerfPatch, Rml::EventId::Mousedown, [this](Rml::Event& _event)
			{
				sendPerfPatchToggle(_event, true);
			});

			juceRmlUi::EventListener::Add(m_btPerfPatch, Rml::EventId::Mouseup, [this](Rml::Event& _event)
			{
				sendPerfPatchToggle(_event, false);
			});

			juceRmlUi::EventListener::Add(m_btPerfPatch, Rml::EventId::Mouseout, [this](Rml::Event& _event)
			{
				sendPerfPatchToggle(_event, false);
			});
		}
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

	void JeLcd::renamePerformance() const
	{
		new juceRmlUi::InplaceEditor(getElement(), m_editor.getJeController().getPatchName(PatchType::Performance),
		[this](const std::string& _newName)
		{
			m_editor.getJeController().changePatchName(PatchType::Performance, _newName);
		});
	}

	void JeLcd::sendPerfPatchToggle(Rml::Event& _event, bool _pressed) const
	{
		m_editor.getJeController().sendButton(jeLib::devices::SwitchType::kSwitch_PerformSel, _pressed);
	}
}

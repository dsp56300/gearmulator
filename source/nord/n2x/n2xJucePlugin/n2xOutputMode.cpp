#include "n2xOutputMode.h"

#include "n2xController.h"
#include "n2xEditor.h"
#include "juceRmlUi/rmlElemComboBox.h"

namespace n2xJucePlugin
{
	static constexpr const char* g_outModesAB[] =
	{
		"A & B Mono/Stereo",
		"A & B Mono",
		"A & B Alternating",
		"A to A / B to B"
	};

	static constexpr const char* g_outModesCD[] =
	{
		"Same as A & B",
		"C & D Mono/Stereo",
		"C & D Mono",
		"C & D Alternating",
		"C to C / D to D"
	};

	OutputMode::OutputMode(const Editor& _editor)
	: m_outAB(_editor.findChild<juceRmlUi::ElemComboBox>("PerfOutModeAB"))
	, m_outCD(_editor.findChild<juceRmlUi::ElemComboBox>("PerfOutModeCD"))
	, m_parameter(_editor.getN2xController().getParameter("PerfOutModeABCD", 0))
	{
		int id = 1;
		for (const auto* mode : g_outModesAB)
			m_outAB->addOption(mode);

		id = 1;
		for (const auto* mode : g_outModesCD)
			m_outCD->addOption(mode);

		juceRmlUi::EventListener::Add(m_outAB, Rml::EventId::Change, [this](Rml::Event&)
		{
			setOutModeAB(static_cast<uint8_t>(m_outAB->getSelectedIndex()));
		});

		juceRmlUi::EventListener::Add(m_outCD, Rml::EventId::Change, [this](Rml::Event&)
		{
			setOutModeCD(static_cast<uint8_t>(m_outCD->getSelectedIndex()));
		});

		m_onOutputModeChanged.set(m_parameter, [this](pluginLib::Parameter* const& _parameter)
		{
			onOutModeChanged(_parameter->getUnnormalizedValue());
		});
	}

	void OutputMode::setOutModeAB(const uint8_t _mode)
	{
		auto v = m_parameter->getUnnormalizedValue();
		v &= 0xf0;
		v |= _mode;
		m_parameter->setUnnormalizedValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);
	}
	 
	void OutputMode::setOutModeCD(const uint8_t _mode)
	{
		auto v = m_parameter->getUnnormalizedValue();
		v &= 0x0f;
		v |= _mode<<4;
		m_parameter->setUnnormalizedValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);
	}

	void OutputMode::onOutModeChanged(pluginLib::ParamValue _paramValue)
	{
		const auto ab = _paramValue & 0xf;
		const auto cd = (_paramValue >> 4) & 0xf;

		m_outAB->setSelectedIndex(ab);
		m_outCD->setSelectedIndex(cd);
	}
}

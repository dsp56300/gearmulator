#include "n2xLfo.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Lfo::Lfo(Editor& _editor, const uint8_t _index)
	: m_editor(_editor)
	, m_index(_index)
	, m_button(_editor.findComponentT<juce::Button>(_index ? "PerfLfo2SyncA" : "PerfLfo1SyncA"))
	{
		m_onCurrentPartChanged.set(_editor.getN2xController().onCurrentPartChanged, [this](const uint8_t&)
		{
			bind();
		});

		bind();
	}

	std::string Lfo::getSyncMultiParamName(const uint8_t _part, const uint8_t _lfoIndex)
	{
		return std::string("PerfLfo") + std::to_string(_lfoIndex+1) + "Sync" + static_cast<char>('A' + _part);
	}

	void Lfo::bind()
	{
		const auto& controller = m_editor.getN2xController();

		auto* syncMultiParam = controller.getParameter(getSyncMultiParamName(controller.getCurrentPart(), m_index), 0);

		m_onSyncMultiParamChanged.set(syncMultiParam, [this](pluginLib::Parameter* const& _parameter)
		{
			updateState(_parameter);
		});

		updateState(syncMultiParam);
	}

	void Lfo::updateState(const pluginLib::Parameter* _param) const
	{
		m_button->setToggleState(_param->getUnnormalizedValue() > 0, juce::dontSendNotification);
	}
}

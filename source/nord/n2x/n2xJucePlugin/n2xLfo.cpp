#include "n2xLfo.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Lfo::Lfo(Editor& _editor, const uint8_t _index)
	: m_editor(_editor)
	, m_index(_index)
	, m_slider(_editor.findComponentT<juce::Slider>(_index ? "PerfLfo2SyncA" : "PerfLfo1SyncA"))
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

	void Lfo::bind() const
	{
		const auto& controller = m_editor.getN2xController();

		const auto paramName = getSyncMultiParamName(controller.getCurrentPart(), m_index);

		const auto paramIdx = controller.getParameterIndexByName(paramName);

		auto& binding = m_editor.getParameterBinding();

		binding.unbind(m_slider);
		binding.bind(*m_slider, paramIdx, 0);
	}

	void Lfo::updateState(const pluginLib::Parameter* _param) const
	{
		m_slider->setValue(_param->getUnnormalizedValue());
	}
}

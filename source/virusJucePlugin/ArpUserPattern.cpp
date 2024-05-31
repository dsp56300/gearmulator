#include "ArpUserPattern.h"

#include "VirusEditor.h"

#include "VirusController.h"

#include "../juceUiLib/uiObjectStyle.h"

namespace genericVirusUI
{
	namespace
	{
		constexpr uint32_t g_listenerId = 0xaa;
	}

	ArpUserPattern::ArpUserPattern(const VirusEditor& _editor) : m_controller(_editor.getController())
	{
		bindParameters();
	}

	void ArpUserPattern::paint(juce::Graphics& g)
	{
		if(!m_patternLength)
			return;

		if(m_gradientStrength <= 0.0f)
		{
			genericUI::UiObjectStyle::parseColor(m_colRectFillActive, getProperties()["colorActive"].toString().toStdString());
			genericUI::UiObjectStyle::parseColor(m_colRectFillInactive, getProperties()["colorInactive"].toString().toStdString());

			m_gradientStrength = getProperties()["gradient"];

			if(m_gradientStrength <= 0.0f)
				m_gradientStrength = 0.5f;
		}

		const auto w = (float)getWidth();
		const auto h = (float)getHeight();

		const auto maxstepW = static_cast<float>(getWidth()) / static_cast<float>(m_steps.size());

		auto makeRectGradient = [&](const juce::Colour& _color)
		{
			const auto& c = _color;
			return juce::ColourGradient::vertical(c, 0.0f, c.darker(m_gradientStrength), h);
		};

		const auto rectGradientActive = makeRectGradient(m_colRectFillActive);
		const auto rectGradientInactive = makeRectGradient(m_colRectFillInactive);

		float x = 0.0f;

		for(int i=0; i<std::min(m_patternLength->getUnnormalizedValue() + 1, static_cast<int>(m_steps.size())); ++i)
		{
			const auto stepW = m_steps[i].length->getValue() * maxstepW;
			const auto stepH = m_steps[i].velocity->getValue() * h;
			const auto y = h - stepH;

			g.setGradientFill(m_steps[i].bitfield->getUnnormalizedValue() > 0 ? rectGradientActive : rectGradientInactive);
			g.fillRect(x, y, stepW, stepH);

			x += maxstepW;
		}
	}

	void ArpUserPattern::onCurrentPartChanged()
	{
		unbindParameters();
		bindParameters();
	}

	void ArpUserPattern::bindParameters()
	{
		for(size_t s=0; s<m_steps.size(); ++s)
		{
			auto& step = m_steps[s];

			step.length = bindParameter("Step " + std::to_string(s+1) +" Length");
			step.velocity = bindParameter("Step " + std::to_string(s+1) +" Velocity");
			step.bitfield = bindParameter("Step " + std::to_string(s+1) +" Bitfield");
		}

		m_patternLength = bindParameter("Arpeggiator/UserPatternLength");
	}

	void ArpUserPattern::unbindParameter(pluginLib::Parameter*& _parameter)
	{
		assert(_parameter);
		_parameter->removeListener(g_listenerId);
		_parameter = nullptr;
	}

	void ArpUserPattern::unbindParameters()
	{
		if(!m_patternLength)
			return;

		unbindParameter(m_patternLength);

		for (auto& s : m_steps)
		{
			unbindParameter(s.length);
			unbindParameter(s.velocity);
			unbindParameter(s.bitfield);
		}
	}

	pluginLib::Parameter* ArpUserPattern::bindParameter(const std::string& _name)
	{
		const auto idx = m_controller.getParameterIndexByName(_name);
		assert(idx != pluginLib::Controller::InvalidParameterIndex);
		auto* p = m_controller.getParameter(idx, m_controller.getCurrentPart());
		assert(p);

		p->onValueChanged.emplace_back(g_listenerId, [this]()
		{
			onParameterChanged();
		});

		return p;
	}

	void ArpUserPattern::onParameterChanged()
	{
		repaint();
	}
}

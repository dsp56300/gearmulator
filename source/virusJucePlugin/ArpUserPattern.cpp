#include "ArpUserPattern.h"

#include "VirusEditor.h"

#include "VirusController.h"

#include "juceUiLib/uiObjectStyle.h"

namespace genericVirusUI
{
	ArpUserPattern::ArpUserPattern(const VirusEditor& _editor) : m_controller(_editor.getController())
	{
		bindParameters();
	}

	void ArpUserPattern::paint(juce::Graphics& g)
	{
		if(!m_patternLength.first)
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

		for(int i=0; i<std::min(m_patternLength.first->getUnnormalizedValue() + 1, static_cast<int>(m_steps.size())); ++i)
		{
			const auto stepW = m_steps[i].length.first->getValue() * maxstepW;
			const auto stepH = m_steps[i].velocity.first->getValue() * h;
			const auto y = h - stepH;

			g.setGradientFill(m_steps[i].bitfield.first->getUnnormalizedValue() > 0 ? rectGradientActive : rectGradientInactive);
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

	void ArpUserPattern::unbindParameter(BoundParam& _parameter)
	{
		assert(_parameter.first);
		_parameter.second.reset();
		_parameter.first = nullptr;
	}

	void ArpUserPattern::unbindParameters()
	{
		if(!m_patternLength.first)
			return;

		unbindParameter(m_patternLength);

		for (auto& s : m_steps)
		{
			unbindParameter(s.length);
			unbindParameter(s.velocity);
			unbindParameter(s.bitfield);
		}
	}

	ArpUserPattern::BoundParam ArpUserPattern::bindParameter(const std::string& _name)
	{
		auto* p = m_controller.getParameter(_name, m_controller.getCurrentPart());
		assert(p);

		return std::make_pair(p, pluginLib::ParameterListener(p, [this](pluginLib::Parameter*)
		{
			onParameterChanged();
		}));
	}

	void ArpUserPattern::onParameterChanged()
	{
		repaint();
	}
}

#include "controllerlink.h"

#include "editor.h"

namespace genericUI
{
	ControllerLink::ControllerLink(std::string _source, std::string _dest, std::string _conditionParam)
		: m_sourceName(std::move(_source))
		, m_destName(std::move(_dest))
		, m_conditionParam(std::move(_conditionParam))
	{
	}

	ControllerLink::~ControllerLink()
	{
		if(m_source)
		{
			m_source->removeListener(this);
			m_source->removeComponentListener(this);
		}
	}

	void ControllerLink::create(const Editor& _editor)
	{
		create(
			_editor.findComponentT<juce::Slider>(m_sourceName),
			_editor.findComponentT<juce::Slider>(m_destName), 
			_editor.findComponentT<juce::Button>(m_conditionParam)
		);
	}

	void ControllerLink::create(juce::Slider* _source, juce::Slider* _dest, juce::Button* _condition)
	{
		m_source = _source;
		m_dest = _dest;
		m_condition = _condition;

		m_lastSourceValue = m_source->getValue();
		m_source->addListener(this);
		m_source->addComponentListener(this);
	}

	void ControllerLink::sliderValueChanged(juce::Slider* _slider)
	{
		const auto current = m_source->getValue();
		const auto delta = current - m_lastSourceValue;
		m_lastSourceValue = current;

		if(!m_sourceIsBeingDragged)
			return;

		if(std::abs(delta) < 0.0001)
			return;

		if(m_condition && !m_condition->getToggleState())
			return;

		const auto destValue = m_dest->getValue();
		const auto newDestValue = destValue + delta;

		if(std::abs(newDestValue - destValue) < 0.0001)
			return;

		m_dest->setValue(newDestValue);
	}

	void ControllerLink::sliderDragStarted(juce::Slider*)
	{
		m_lastSourceValue = m_source->getValue();
		m_sourceIsBeingDragged = true;
	}

	void ControllerLink::sliderDragEnded(juce::Slider*)
	{
		m_sourceIsBeingDragged = false;
	}

	void ControllerLink::componentBeingDeleted(juce::Component& component)
	{
		m_source = nullptr;
	}
}

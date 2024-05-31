#include "focusedParameter.h"

#include "pluginEditor.h"

#include "../jucePluginLib/controller.h"
#include "../jucePluginLib/parameterbinding.h"

namespace jucePluginEditorLib
{
	FocusedParameter::FocusedParameter(const pluginLib::Controller& _controller, const pluginLib::ParameterBinding& _parameterBinding, const genericUI::Editor& _editor)
		: m_parameterBinding(_parameterBinding)
		, m_controller(_controller)
	{
		m_focusedParameterName = _editor.findComponentT<juce::Label>("FocusedParameterName", false);
		m_focusedParameterValue = _editor.findComponentT<juce::Label>("FocusedParameterValue", false);

		if (m_focusedParameterName)
			m_focusedParameterName->setVisible(false);
		if (m_focusedParameterValue)
			m_focusedParameterValue->setVisible(false);

		m_tooltip.reset(new FocusedParameterTooltip(_editor.findComponentT<juce::Label>("FocusedParameterTooltip", false), _editor));

		updateControlLabel(nullptr);

		for (auto& params : m_controller.getExposedParameters())
		{
			for (const auto& param : params.second)
			{
				m_boundParameters.insert({param, pluginLib::EventListener(param->onValueChanged, [this](const pluginLib::Parameter* _param)
				{
					if (_param->getChangeOrigin() == pluginLib::Parameter::ChangedBy::PresetChange || 
						_param->getChangeOrigin() == pluginLib::Parameter::ChangedBy::Derived)
						return;
					if(auto* comp = m_parameterBinding.getBoundComponent(_param))
						updateControlLabel(comp);
				})});
			}
		}
	}

	FocusedParameter::~FocusedParameter()
	{
		m_tooltip.reset();
	
		m_boundParameters.clear();
	}

	void FocusedParameter::onMouseEnter(const juce::MouseEvent& _event)
	{
		auto* component = _event.eventComponent;

		if(component && component->getProperties().contains("parameter"))
			updateControlLabel(component);
	}

	void FocusedParameter::updateParameter(const std::string& _name, const std::string& _value)
	{
		if (m_focusedParameterName)
		{
			if(_name.empty())
			{
				m_focusedParameterName->setVisible(false);
			}
			else
			{
				m_focusedParameterName->setText(_name, juce::dontSendNotification);
				m_focusedParameterName->setVisible(true);
			}
		}
		if(m_focusedParameterValue)
		{
			if(_value.empty())
			{
				m_focusedParameterValue->setVisible(false);
			}
			else
			{
				m_focusedParameterValue->setText(_value, juce::dontSendNotification);
				m_focusedParameterValue->setVisible(true);
			}
		}
	}

	void FocusedParameter::timerCallback()
	{
		updateControlLabel(nullptr);
	}

	void FocusedParameter::updateControlLabel(juce::Component* _component)
	{
		stopTimer();

		if(_component)
		{
			// combo boxes report the child label as event source, try the parent in this case
			if(!_component->getProperties().contains("parameter"))
				_component = _component->getParentComponent();
		}

		if(!_component || !_component->getProperties().contains("parameter"))
		{
			updateParameter({}, {});
			m_tooltip->setVisible(false);
			return;
		}

		const auto& props = _component->getProperties();
		const int v = props["parameter"];

		const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(m_controller.getCurrentPart());

		const auto* p = m_controller.getParameter(v, static_cast<uint8_t>(part));

		// do not show soft knob parameter if the softknob is bound to another parameter
		if(p && p->getDescription().isSoftKnob())
		{
			const auto* softKnob = m_controller.getSoftknob(p);
			if(softKnob && softKnob->isBound())
				p = softKnob->getTargetParameter();
		}
		if(!p)
		{
			updateParameter({}, {});
			m_tooltip->setVisible(false);
			return;
		}

		const auto value = p->getText(p->getValue(), 0).toStdString();

		const auto& desc = p->getDescription();

		updateParameter(desc.displayName, value);

		m_tooltip->initialize(_component, value);

		if(!m_tooltip->isValid())
			return;

		const auto tooltipTime = m_tooltip->getTooltipDisplayTime();

		startTimer(tooltipTime == 0 ? 1500 : static_cast<int>(tooltipTime));
	}
}

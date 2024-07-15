#include "focusedParameter.h"

#include "pluginEditor.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"
#include "jucePluginLib/parameterbinding.h"

namespace jucePluginEditorLib
{
	constexpr int g_defaultDisplayTimeout = 1500;	// milliseconds

	namespace
	{
		FocusedParameter::Priority getPriority(pluginLib::Parameter::Origin _origin)
		{
			switch (_origin)
			{
			case pluginLib::Parameter::Origin::Unknown:
			case pluginLib::Parameter::Origin::PresetChange:
			case pluginLib::Parameter::Origin::Derived:
			default:
				return FocusedParameter::Priority::Low;
			case pluginLib::Parameter::Origin::Midi:
			case pluginLib::Parameter::Origin::HostAutomation:
				return FocusedParameter::Priority::Medium;
			case pluginLib::Parameter::Origin::Ui:
				return FocusedParameter::Priority::High;
			}
		}

		FocusedParameter::Priority getPriority(const pluginLib::Parameter* _param)
		{
			if(!_param)
				return FocusedParameter::Priority::None;
			return getPriority(_param->getChangeOrigin());
		}

		bool operator < (const FocusedParameter::Priority _a, const FocusedParameter::Priority _b)
		{
			return static_cast<int>(_a) < static_cast<int>(_b);
		}
	}

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

		updateControlLabel(nullptr, Priority::None);

		for (auto& params : m_controller.getExposedParameters())
		{
			for (const auto& param : params.second)
			{
				m_boundParameters.insert({param, pluginLib::ParameterListener(param, [this](const pluginLib::Parameter* _param)
				{
					if (_param->getChangeOrigin() == pluginLib::Parameter::Origin::PresetChange || 
						_param->getChangeOrigin() == pluginLib::Parameter::Origin::Derived)
						return;
					if(auto* comp = m_parameterBinding.getBoundComponent(_param))
						updateControlLabel(comp, _param);
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
			updateControlLabel(component, Priority::High);
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
		updateControlLabel(nullptr, Priority::High);
	}

	void FocusedParameter::updateControlLabel(juce::Component* _component, const Priority _prio)
	{
		if(_component)
		{
			// combo boxes report the child label as event source, try the parent in this case
			if(!_component->getProperties().contains("parameter"))
				_component = _component->getParentComponent();
		}

		const auto* param = getParameterFromComponent(_component);

		updateControlLabel(_component, param, _prio);
	}

	void FocusedParameter::updateControlLabel(juce::Component* _component, const pluginLib::Parameter* _param, Priority _priority)
	{
		if(_priority < m_currentPriority)
			return;

		stopTimer();

		if(!_param || !_component)
		{
			m_currentPriority = Priority::None;
			updateParameter({}, {});
			m_tooltip->setVisible(false);
			return;
		}

		const auto value = _param->getText(_param->getValue(), 0).toStdString();

		updateParameter(_param->getDescription().displayName, value);

		m_tooltip->initialize(_component, value);

		const auto tooltipTime = m_tooltip && m_tooltip->isValid() ? m_tooltip->getTooltipDisplayTime() : g_defaultDisplayTimeout;

		startTimer(tooltipTime == 0 ? g_defaultDisplayTimeout : static_cast<int>(tooltipTime));

		m_currentPriority = _priority;
	}

	void FocusedParameter::updateControlLabel(juce::Component* _component, const pluginLib::Parameter* _param)
	{
		return updateControlLabel(_component, _param, getPriority(_param));
	}

	const pluginLib::Parameter* FocusedParameter::getParameterFromComponent(juce::Component* _component) const
	{
		if(!_component || !_component->getProperties().contains("parameter"))
			return nullptr;

		const auto& props = _component->getProperties();
		const int paramIdx = props["parameter"];

		const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(m_controller.getCurrentPart());

		const auto* param = m_controller.getParameter(paramIdx, static_cast<uint8_t>(part));

		// do not show soft knob parameter if the softknob is bound to another parameter
		if(param && param->getDescription().isSoftKnob())
		{
			const auto* softKnob = m_controller.getSoftknob(param);
			if(softKnob && softKnob->isBound())
				return softKnob->getTargetParameter();
		}
		return param;
	}
}

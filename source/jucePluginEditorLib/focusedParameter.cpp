#include "focusedParameter.h"

#include "pluginEditor.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

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

	FocusedParameter::FocusedParameter(const pluginLib::Controller& _controller, const Editor& _editor)
		: m_parameterBinding(*_editor.getRmlParameterBinding())
		, m_controller(_controller)
	{
		m_focusedParameterName = _editor.findChild("FocusedParameterName", false);
		m_focusedParameterValue = _editor.findChild("FocusedParameterValue", false);

		if (m_focusedParameterName)
			juceRmlUi::helper::setVisible(m_focusedParameterName, false);
		if (m_focusedParameterValue)
			juceRmlUi::helper::setVisible(m_focusedParameterValue, false);

		m_tooltip.reset(new FocusedParameterTooltip(_editor.findChild("FocusedParameterTooltip", false)));

		updateControlLabel(nullptr, Priority::None);

		auto bindParameter = [this](pluginLib::Parameter* _p)
		{
			m_boundParameters.insert({ _p, pluginLib::ParameterListener(_p, [this](const pluginLib::Parameter* _param)
			{
				if (_param->getChangeOrigin() == pluginLib::Parameter::Origin::PresetChange || 
					_param->getChangeOrigin() == pluginLib::Parameter::Origin::Derived)
					return;
				if (auto* comp = m_parameterBinding.getElementForParameter(_param))
					updateControlLabel(comp, _param);
			})});
		};

		for (auto& params : m_controller.getExposedParameters())
		{
			for (const auto& param : params.second)
				bindParameter(param);
		}

		for (auto& params : m_controller.getInternalParameters())
		{
			for (const auto& param : params.second)
				bindParameter(param);
		}

		m_mouseOver.add(_editor.getDocument(), Rml::EventId::Mouseover, [this](const Rml::Event& _event)
		{
			if (auto* element = _event.GetTargetElement())
				updateControlLabel(element, Priority::High);
		});
	}

	FocusedParameter::~FocusedParameter()
	{
		m_mouseOver.reset();

		m_tooltip.reset();
	
		m_boundParameters.clear();
	}

	void FocusedParameter::updateByElement(const Rml::Element* _element)
	{
		updateControlLabel(_element, Priority::High);
	}

	void FocusedParameter::updateParameter(const std::string& _name, const std::string& _value)
	{
		if (m_focusedParameterName)
		{
			if(_name.empty())
			{
				juceRmlUi::helper::setVisible(m_focusedParameterName, false);
			}
			else
			{
				m_focusedParameterName->SetInnerRML(Rml::StringUtilities::EncodeRml(_name));
				juceRmlUi::helper::setVisible(m_focusedParameterName, true);
			}
		}
		if(m_focusedParameterValue)
		{
			if(_value.empty())
			{
				juceRmlUi::helper::setVisible(m_focusedParameterValue, false);
			}
			else
			{
				m_focusedParameterValue->SetInnerRML(Rml::StringUtilities::EncodeRml(_value));
				juceRmlUi::helper::setVisible(m_focusedParameterValue, true);
			}
		}
	}

	void FocusedParameter::timerCallback()
	{
		updateControlLabel(nullptr, Priority::High);
	}

	void FocusedParameter::updateControlLabel(const Rml::Element* _elem, const Priority _prio)
	{
		if (_elem)
		{
			if (auto* parent = _elem->GetParentNode())
			{
				if (dynamic_cast<Rml::ElementFormControlInput*>(parent))
					_elem = parent;
			}
		}

		const auto* param = getParameterFromElement(_elem);

		updateControlLabel(_elem, param, _prio);
	}

	void FocusedParameter::updateControlLabel(const Rml::Element* _elem, const pluginLib::Parameter* _param, Priority _priority)
	{
		if(_priority < m_currentPriority)
			return;

		_param = resolveSoftKnob(_param);

		if(!_param || !_elem)
		{
			stopTimer();
			m_currentParameter = nullptr;
			m_currentPriority = Priority::None;
			updateParameter({}, {});
			m_tooltip->setVisible(false);
			return;
		}

		stopTimer();

		const auto tooltipTime = m_tooltip && m_tooltip->isValid() ? m_tooltip->getTooltipDisplayTime() : g_defaultDisplayTimeout;

		startTimer(tooltipTime == 0 ? g_defaultDisplayTimeout : static_cast<int>(tooltipTime));

		// do not allow the displayed parameter to change on same priority to reduce flickering
		// if multiple values are automated. Only allow for UI as it should immediately reflect user changes
		if (_priority != getPriority(pluginLib::Parameter::Origin::Ui) && _priority == m_currentPriority && m_currentParameter != _param)
			return;

		m_currentParameter = _param;

		const auto value = _param->getText(_param->getValue(), 0).toStdString();

		updateParameter(_param->getDescription().displayName, value);

		m_tooltip->initialize(_elem, value);

		m_currentPriority = _priority;
	}

	void FocusedParameter::updateControlLabel(const Rml::Element* _elem, const pluginLib::Parameter* _param)
	{
		return updateControlLabel(_elem, _param, getPriority(_param));
	}

	const pluginLib::Parameter* FocusedParameter::getParameterFromElement(const Rml::Element* _elem) const
	{
		if (!_elem)
			return {};
		return m_parameterBinding.getParameterForElement(_elem);
	}

	const pluginLib::Parameter* FocusedParameter::resolveSoftKnob(const pluginLib::Parameter* _sourceParam) const
	{
		if(_sourceParam && _sourceParam->getDescription().isSoftKnob())
		{
			const auto* softKnob = m_controller.getSoftknob(_sourceParam);
			if(softKnob && softKnob->isBound())
				return softKnob->getTargetParameter();
		}
		return _sourceParam;
	}
}

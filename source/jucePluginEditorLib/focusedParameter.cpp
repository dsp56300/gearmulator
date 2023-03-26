#include "focusedParameter.h"

#include "pluginEditor.h"

#include "../jucePluginLib/controller.h"
#include "../jucePluginLib/parameterbinding.h"

namespace jucePluginEditorLib
{
	static constexpr uint32_t g_listenerId = 1;

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

		m_tooltip.reset(new FocusedParameterTooltip(_editor.findComponentT<juce::Label>("FocusedParameterTooltip", false)));

		updateControlLabel(nullptr);

		for (auto& params : m_controller.getExposedParameters())
		{
			for (const auto& param : params.second)
			{
				m_boundParameters.push_back(param);

				param->onValueChanged.emplace_back(g_listenerId, [this, param]()
				{
					if (param->getChangeOrigin() == pluginLib::Parameter::ChangedBy::PresetChange || 
						param->getChangeOrigin() == pluginLib::Parameter::ChangedBy::Derived)
						return;
					auto* comp = m_parameterBinding.getBoundComponent(param);
					if(comp)
						updateControlLabel(comp);
				});
			}
		}
	}

	FocusedParameter::~FocusedParameter()
	{
		m_tooltip.reset();

		for (auto* p : m_boundParameters)
			p->removeListener(g_listenerId);
	}

	void FocusedParameter::onMouseEnter(const juce::MouseEvent& _event)
	{
		auto* component = _event.eventComponent;

		if(component && component->getProperties().contains("parameter"))
			updateControlLabel(component);
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
			if (m_focusedParameterName)
				m_focusedParameterName->setVisible(false);
			if (m_focusedParameterValue)
				m_focusedParameterValue->setVisible(false);
			m_tooltip->setVisible(false);
			return;
		}

		const auto& props = _component->getProperties();
		const int v = props["parameter"];

		const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(m_controller.getCurrentPart());

		const auto* p = m_controller.getParameter(v, static_cast<uint8_t>(part));

		if(!p)
		{
			if (m_focusedParameterName)
				m_focusedParameterName->setVisible(false);
			if (m_focusedParameterValue)
				m_focusedParameterValue->setVisible(false);
			m_tooltip->setVisible(false);
			return;
		}

		const auto value = p->getText(p->getValue(), 0);

		const auto& desc = p->getDescription();

		if (m_focusedParameterName)
		{
			m_focusedParameterName->setText(desc.displayName, juce::dontSendNotification);
			m_focusedParameterName->setVisible(true);
		}

		if (m_focusedParameterValue)
		{
			m_focusedParameterValue->setText(value, juce::dontSendNotification);
			m_focusedParameterValue->setVisible(true);
		}

		m_tooltip->initialize(_component, value);

		startTimer(3000);
	}
}

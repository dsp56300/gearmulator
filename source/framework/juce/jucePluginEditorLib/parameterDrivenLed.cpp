#include "parameterDrivenLed.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"

namespace jucePluginEditorLib
{
	ParameterDrivenLed::ParameterDrivenLed(Editor& _editor, const std::string& _elementId, std::string _parameter, const uint8_t _part, IsOn _isOn, OnClick _onClick, OnUpdate _onUpdate)
		: m_editor(_editor)
		, m_parameterName(std::move(_parameter))
		, m_part(_part)
		, m_led(_editor.findChild(_elementId))
		, m_isOn(std::move(_isOn))
		, m_onClick(std::move(_onClick))
		, m_onUpdate(std::move(_onUpdate))
	{
		if (m_onClick)
		{
			juceRmlUi::EventListener::AddClick(m_led, [this]
			{
				if (m_param)
					m_onClick(*m_param, juceRmlUi::ElemButton::isChecked(m_led));
			});
		}
		else
		{
			m_led->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		}

		if (m_part == CurrentPart)
		{
			m_onCurrentPartChanged.set(m_editor.getProcessor().getController().onCurrentPartChanged, [this](const uint8_t&)
			{
				bind();
			});
		}

		bind();
	}

	void ParameterDrivenLed::bind()
	{
		const auto& c = m_editor.getProcessor().getController();

		m_param = c.getParameter(m_parameterName, m_part == CurrentPart ? c.getCurrentPart() : m_part);

		if (!m_param)
		{
			m_onParamChanged.reset();
			return;
		}

		m_onParamChanged.set(m_param, [this](const pluginLib::Parameter*)
		{
			update();
		});

		update();
	}

	void ParameterDrivenLed::update() const
	{
		juceRmlUi::ElemButton::setChecked(m_led, m_isOn(*m_param));

		if (m_onUpdate)
			m_onUpdate(*m_led, *m_param);
	}
}

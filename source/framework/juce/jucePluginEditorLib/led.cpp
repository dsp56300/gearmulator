#include "led.h"

#include "pluginEditor.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

namespace jucePluginEditorLib
{
	Led::Led(const Editor& _editor, Rml::Element* _targetAlpha, Rml::Element* _targetInvAlpha)
	: m_editor(_editor)
	, m_targetAlpha(_targetAlpha)
	, m_targetInvAlpha(_targetInvAlpha)
	{
	}

	void Led::setValue(const float _v)
	{
		if(m_value == _v)  // NOLINT(clang-diagnostic-float-equal)
			return;

		m_value = _v;

		repaint();
	}

	void Led::setSourceCallback(SourceCallback&& _func)
	{
		m_sourceCallback = std::move(_func);

		if(m_sourceCallback)
		{
			m_onPreUpdate.set(m_editor.getRmlComponent()->evPreUpdate, [this](juceRmlUi::RmlComponent*)
			{
				setValue(m_sourceCallback());
			});
			m_onPostUpdate.set(m_editor.getRmlComponent()->evPostUpdate, [this](juceRmlUi::RmlComponent*)
			{
				m_targetAlpha->GetContext()->RequestNextUpdate(0.0f);
			});
		}
		else
		{
			m_onPreUpdate.reset();
			m_onPostUpdate.reset();
		}
	}

	void Led::repaint() const
	{
		m_targetAlpha->SetProperty(Rml::PropertyId::Opacity, Rml::Property(m_value, Rml::Unit::NUMBER));

		if (m_targetInvAlpha)
			m_targetInvAlpha->SetProperty(Rml::PropertyId::Opacity, Rml::Property(1.0f - m_value, Rml::Unit::NUMBER));

		if (m_targetAlpha->IsVisible(true))
			m_editor.getRmlComponent()->enqueueUpdate();
	}
}

#include "focusedParameterTooltip.h"

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementUtilities.h"

namespace jucePluginEditorLib
{
	FocusedParameterTooltip::FocusedParameterTooltip(Rml::Element* _label) : m_label(_label)
	{
		setVisible(false);

		if(isValid())
		{
			m_label->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			m_label->SetProperty(Rml::PropertyId::Position, Rml::Style::Position::Absolute);
		}
	}

	void FocusedParameterTooltip::setVisible(bool _visible) const
	{
		if(isValid())
			juceRmlUi::helper::setVisible(m_label, _visible);
	}

	uint32_t FocusedParameterTooltip::getTooltipDisplayTime() const
	{
		if (!isValid())
			return 0;

		const auto* attrib = m_label->GetAttribute("displayTime");

		if (!attrib)
			return 0;

		const int time = attrib->Get<int>(m_label->GetCoreInstance());
		return time > 0 ? static_cast<uint32_t>(time) : 0;
	}

	void FocusedParameterTooltip::initialize(const Rml::Element* _component, const std::string& _value) const
	{
		if(!isValid())
			return;

		if(_component->IsVisible())
		{
			auto* comp = const_cast<Rml::Element*>(_component);

			const auto box = comp->GetBox();

			float x = comp->GetAbsoluteLeft();
			float y = comp->GetAbsoluteTop();

			const auto textWidth = Rml::ElementUtilities::GetStringWidth(m_label, _value);

			const auto labelWidth = static_cast<float>(textWidth);

			x += box.GetSize().x * 0.5f - labelWidth * 0.5f;
			y += box.GetSize().y + (m_label->GetBox().GetSize().y * 0.5f);

			if(auto* attribOffset = m_label->GetAttribute("offsetY"))
				y += attribOffset->Get<float>(m_label->GetCoreInstance());

			m_label->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
			m_label->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
			m_label->SetProperty(Rml::PropertyId::Width, Rml::Property(labelWidth, Rml::Unit::PX));

			m_label->SetInnerRML(_value);

			juceRmlUi::helper::setVisible(m_label, true);
		}
		else if(m_label)
		{
			juceRmlUi::helper::setVisible(m_label, false);
		}
	}
}

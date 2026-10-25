#include "focusedParameterTooltip.h"

#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ElementUtilities.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib
{
	namespace
	{
		// Only knobs and sliders get a tooltip, as with the JUCE skins (juce::Slider only). A button or a combo box
		// shows its value itself, and its box can be much larger than what it draws, which put the tooltip next to
		// other controls (BUG-10382)
		bool hasTooltip(const Rml::Element* _component)
		{
			if (dynamic_cast<const juceRmlUi::ElemKnob*>(_component))
				return true;
			const auto* input = dynamic_cast<const Rml::ElementFormControlInput*>(_component);
			return input && input->GetAttribute("type", std::string()) == "range";
		}
	}

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

		if(hasTooltip(_component) && _component->IsVisible(true))
		{
			auto* comp = const_cast<Rml::Element*>(_component);

			const auto box = comp->GetBox();

			float x = comp->GetAbsoluteLeft();
			float y = comp->GetAbsoluteTop();

			const auto textWidth = Rml::ElementUtilities::GetStringWidth(m_label, _value);

			const auto labelWidth = static_cast<float>(textWidth) + 8;

			x += box.GetSize().x * 0.5f - labelWidth * 0.5f;
			y += box.GetSize().y;

			if(auto* attribOffset = m_label->GetAttribute("offsetY"))
				y += attribOffset->Get<float>(m_label->GetCoreInstance());

			m_label->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
			m_label->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
			m_label->SetProperty(Rml::PropertyId::Width, Rml::Property(labelWidth, Rml::Unit::PX));

			m_label->SetInnerRML(_value);

			// clamp height to not exceed height of whole document
			const auto docHeight = m_label->GetOwnerDocument()->GetClientHeight();
			const auto height = m_label->GetBox().GetSize(Rml::BoxArea::Margin).y;
			if (y + height > docHeight)
				m_label->SetProperty(Rml::PropertyId::Top, Rml::Property(docHeight - height, Rml::Unit::PX));

			juceRmlUi::helper::setVisible(m_label, true);
		}
		else if(m_label)
		{
			juceRmlUi::helper::setVisible(m_label, false);
		}
	}
}

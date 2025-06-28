#include "rmlElemComboBox.h"

#include "rmlHelper.h"
#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"

namespace juceRmlUi
{
	ElemComboBox::ElemComboBox(const Rml::String& _tag) : ElemValue(_tag)
	{
		AddEventListener(Rml::EventId::Click, this);
	}

	ElemComboBox::~ElemComboBox()
	{
		RemoveEventListener(Rml::EventId::Click, this);
	}

	void ElemComboBox::setOptions(const std::vector<Rml::String>& _options)
	{
		m_options = _options;
	}

	void ElemComboBox::onChangeValue()
	{
		ElemValue::onChangeValue();
		updateValueText();
	}

	void ElemComboBox::ProcessEvent(Rml::Event& _event)
	{
		if (_event.GetId() == Rml::EventId::Click)
		{
			onClick(_event);
		}
	}

	void ElemComboBox::onClick(const Rml::Event&)
	{
		if (m_options.empty())
			return;

		m_menu.reset(new Menu());

		const auto currentValue = static_cast<uint32_t>(getValue());

		for (uint32_t i=0; i<=static_cast<uint32_t>(getMaxValue()); ++i)
		{
			auto& option = m_options[i];

			m_menu->addEntry(option, i == currentValue, [this, i]
			{
				setValue(static_cast<float>(i));
			});
		}

		m_menu->open(this, GetAbsoluteOffset(Rml::BoxArea::Border), getProperty("items-per-column", 16));
	}

	void ElemComboBox::updateValueText()
	{
		if (m_options.empty())
			return;

		const auto value = getValue();

		if (value < 0 || value >= static_cast<float>(m_options.size()))
			return;

		const auto& option = m_options[static_cast<size_t>(value)];
		const auto text = Rml::StringUtilities::EncodeRml(option);

//		SetProperty("decorator", "text(\"" + text + "\" inherit-color left center) content-box");
		if (m_textElem == nullptr)
		{
			// it might exist already via rml, find it and use it
			for (auto i=0; i<GetNumChildren(); ++i)
			{
				auto* child = GetChild(i);
				if (child->GetTagName() != "combotext")
					continue;
				m_textElem = child;
				break;
			}

			if (!m_textElem)
			{
				if (text.empty())
					return;

				SetInnerRML({});

				auto textElem = GetOwnerDocument()->CreateElement("combotext");

				m_textElem = AppendChild(std::move(textElem));
			}
		}

		m_textElem->SetInnerRML(text);
	}
}

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

	void ElemComboBox::onClick(const Rml::Event& _event)
	{
		if (m_options.empty())
			return;

		auto* menu = new Menu();

		const auto currentValue = static_cast<uint32_t>(getValue());

		for (uint32_t i=0; i<static_cast<uint32_t>(getMaxValue()); ++i)
		{
			auto& option = m_options[i];

			menu->addEntry(option, i == currentValue, [this, i]
			{
				setValue(static_cast<float>(i));
			});
		}

		menu->open(this, GetAbsoluteOffset(Rml::BoxArea::Border), getProperty("items-per-column", 16));
	}

	void ElemComboBox::updateValueText()
	{
		if (m_options.empty())
			return;
		const auto value = getValue();
		if (value < 0 || value >= static_cast<float>(m_options.size()))
			return;
		const auto& option = m_options[static_cast<size_t>(value)];
		SetInnerRML(Rml::StringUtilities::EncodeRml(option));
	}
}

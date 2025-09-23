#include "rmlElemComboBox.h"

#include <algorithm>

#include "rmlHelper.h"
#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	ElemComboBox::ElemComboBox(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : ElemValue(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Click, this);
	}

	ElemComboBox::~ElemComboBox()
	{
		RemoveEventListener(Rml::EventId::Click, this);
	}

	void ElemComboBox::setEntries(const std::vector<Entry>& _options)
	{
		m_options = _options;

		if (m_options.empty())
		{
			setMaxValue(0.0f);
			return;
		}

		int max = m_options.front().value;

		for (const auto& option : m_options)
			max = std::max(option.value, max);

		setMaxValue(static_cast<float>(max));
	}

	void ElemComboBox::setOptions(const std::vector<Rml::String>& _options)
	{
		if (_options.empty())
		{
			m_options.clear();
			setMaxValue(0.0f);
			return;
		}

		m_options.clear();

		m_options.reserve(_options.size());

		for (size_t i = 0; i < _options.size(); ++i)
		{
			if (_options[i].empty())
				continue;
			m_options.push_back({_options[i], static_cast<int>(i)});
		}

		setMaxValue(static_cast<float>(m_options.size() - 1));
	}

	void ElemComboBox::addOption(const Rml::String& _option)
	{
		m_options.push_back({ _option, static_cast<int>(m_options.size())});
		setMaxValue(static_cast<float>(m_options.size() - 1));
	}

	void ElemComboBox::clearOptions()
	{
		m_options.clear();
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

		const auto currentValue = static_cast<int>(getValue());

		for (auto& option : m_options)
		{
			if (option.text.empty())
				continue;

			m_menu->addEntry(option.text, option.value == currentValue, [this, v = option.value]
			{
				setValue(static_cast<float>(v));
			});
		}

		m_menu->open(this, GetAbsoluteOffset(Rml::BoxArea::Border), getProperty("items-per-column", 16));
	}

	void ElemComboBox::setSelectedIndex(const size_t _index, const bool _sendChangeEvent/* = true*/)
	{
		setValue(static_cast<float>(_index), _sendChangeEvent);

		if (!_sendChangeEvent)
			updateValueText();
	}

	int ElemComboBox::getSelectedIndex() const
	{
		const auto i = static_cast<int>(getValue());
		if (i < 0|| static_cast<size_t>(i) >= m_options.size())
			return -1;
		return i;
	}

	void ElemComboBox::OnUpdate()
	{
		ElemValue::OnUpdate();

		if (m_valueTextDirty || !m_textElem)
		{
			if (updateValueText())
				m_valueTextDirty = false;
		}
	}

	bool ElemComboBox::updateValueText()
	{
		if (m_options.empty())
			return false;

		if (!GetOwnerDocument())
		{
			m_valueTextDirty = true;
			return false;
		}

		const auto value = getValue();

		std::string text;

		for (const auto & option : m_options)
		{
			if (option.value == static_cast<int>(value))
			{
				text = option.text;
				break;
			}
		}

		text = Rml::StringUtilities::EncodeRml(text);

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
					return true;

				SetInnerRML({});

				auto textElem = GetOwnerDocument()->CreateElement("combotext");

				m_textElem = AppendChild(std::move(textElem));
			}
		}

		m_textElem->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		m_textElem->SetInnerRML(text);

		return true;
	}
}

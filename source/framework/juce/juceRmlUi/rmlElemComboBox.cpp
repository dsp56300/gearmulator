#include "rmlElemComboBox.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "rmlHelper.h"
#include "rmlPopupWindow.h"
#include "juceRmlComponent.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juceRmlUi
{
	ElemComboBox::ElemComboBox(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : ElemValue(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Click, this);
		AddEventListener(Rml::EventId::Mousescroll, this);
		AddEventListener(Rml::EventId::Mousedown, this);
		AddEventListener(Rml::EventId::Drag, this);
	}

	ElemComboBox::~ElemComboBox()
	{
		RemoveEventListener(Rml::EventId::Click, this);
		RemoveEventListener(Rml::EventId::Mousescroll, this);
		RemoveEventListener(Rml::EventId::Mousedown, this);
		RemoveEventListener(Rml::EventId::Drag, this);
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

		// new texts for the same value
		updateValueText();
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

		int max = 0;
		for (const auto& option : m_options)
			max = std::max(option.value, max);
		setMaxValue(static_cast<float>(max));
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
			// the release that ends a drag is no click
			if (m_dragged)
				m_dragged = false;
			else
				onClick(_event);
		}
		else if (_event.GetId() == Rml::EventId::Mousescroll)
		{
			onMouseScroll(_event);
		}
		else if (_event.GetId() == Rml::EventId::Mousedown)
		{
			onMouseDown(_event);
		}
		else if (_event.GetId() == Rml::EventId::Drag)
		{
			onDrag(_event);
		}
	}

	void ElemComboBox::OnAttributeChange(const Rml::ElementAttributes& _changedAttributes)
	{
		ElemValue::OnAttributeChange(_changedAttributes);

		if (_changedAttributes.find("drag-step") == _changedAttributes.end())
			return;

		m_dragStep = std::max(0.0f, GetAttribute<float>("drag-step", 0.0f));
		if (m_dragStep > 0.0f)
			SetProperty(Rml::PropertyId::Drag, Rml::Property(Rml::Style::Drag::Drag));
		else
			RemoveProperty(Rml::PropertyId::Drag);
	}

	void ElemComboBox::onMouseDown(const Rml::Event& _event)
	{
		m_dragged = false;
		if (m_dragStep <= 0.0f || helper::getMouseButton(_event) != MouseButton::Left)
			return;
		m_dragStart = helper::getMousePos(_event);
		m_dragStartIndex = getSelectedIndex();
	}

	void ElemComboBox::onDrag(const Rml::Event& _event)
	{
		if (m_dragStep <= 0.0f || m_dragStartIndex < 0)
			return;

		const auto delta = helper::getMousePos(_event) - m_dragStart;
		const auto step = m_dragStep * GetContext()->GetDensityIndependentPixelRatio();
		const auto steps = static_cast<int>(std::trunc((delta.x - delta.y) / step));

		// a press that wobbles a little still opens the menu
		if (!m_dragged && steps == 0)
			return;
		m_dragged = true;

		const auto index = stepIndex(m_dragStartIndex, steps);
		if (index >= 0 && index != getSelectedIndex())
			setSelectedIndex(static_cast<size_t>(index));
	}

	int ElemComboBox::stepIndex(const int _index, const int _steps) const
	{
		const int offset = _steps < 0 ? -1 : 1;
		int result = _index;
		int remaining = std::abs(_steps);

		for (int i = _index + offset; remaining > 0 && i >= 0 && static_cast<size_t>(i) < m_options.size(); i += offset)
		{
			if (m_options[static_cast<size_t>(i)].hidden)
				continue;
			result = i;
			--remaining;
		}
		return result;
	}

	void ElemComboBox::onClick(const Rml::Event&)
	{
		if (m_options.empty())
			return;

		if (GetAttribute<Rml::String>("popup", "inline") == "window")
		{
			openPopupWindow();
			return;
		}

		m_menu.reset(new Menu());

		const auto currentValue = static_cast<int>(getValue());

		std::set<std::string> seenLabels;
		std::vector<const Entry*> shown;

		for (auto& option : m_options)
		{
			if (option.text.empty() || option.hidden)
				continue;

			if (m_collapseDuplicates && !seenLabels.insert(option.text).second)
				continue;

			shown.push_back(&option);
		}

		const auto addEntry = [this, currentValue](Menu& _menu, const Entry& _option)
		{
			_menu.addEntry(_option.text, _option.value == currentValue, [this, v = _option.value]
			{
				setValue(static_cast<float>(v));
			});
		};

		const auto groupSize = static_cast<size_t>(std::max(0, GetAttribute<int>("group-size", 0)));

		if (groupSize > 0 && shown.size() > groupSize)
		{
			const auto firstWord = [](const std::string& _text) { return _text.substr(0, _text.find(' ')); };

			for (size_t first = 0; first < shown.size(); first += groupSize)
			{
				const auto last = std::min(first + groupSize, shown.size()) - 1;
				auto group = std::make_shared<Menu>();
				group->setItemsPerColumn(static_cast<uint32_t>(groupSize));
				bool current = false;
				for (size_t i = first; i <= last; ++i)
				{
					addEntry(*group, *shown[i]);
					current |= shown[i]->value == currentValue;
				}
				m_menu->addSubMenu(firstWord(shown[first]->text) + " - " + firstWord(shown[last]->text), group, current);
			}
		}
		else
		{
			for (const auto* option : shown)
				addEntry(*m_menu, *option);
		}

		m_menu->open(this, GetAbsoluteOffset(Rml::BoxArea::Border), getProperty("items-per-column", 16));
	}

	void ElemComboBox::openPopupWindow()
	{
		auto* component = RmlComponent::fromElement(this);
		if (!component)
			return;

		juce::PopupMenu menu;

		const auto currentValue = static_cast<int>(getValue());

		std::set<std::string> seenLabels;

		for (size_t i = 0; i < m_options.size(); ++i)
		{
			const auto& option = m_options[i];

			if (option.text.empty() || option.hidden)
				continue;

			if (m_collapseDuplicates && !seenLabels.insert(option.text).second)
				continue;

			// item ids must be positive, 0 is "dismissed"
			menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(option.text.c_str()), true, option.value == currentValue);
		}

		// The document may be reloaded while the popup is up - a driver change rebuilds the
		// settings - so the element is observed, not captured.
		auto observer = Rml::Element::GetObserverPtr(GetCoreInstance());
		popupWindow::show(menu, *component, GetAbsoluteOffset(Rml::BoxArea::Border),
			GetBox().GetSize(Rml::BoxArea::Border), [observer = std::move(observer)](const int _result)
			{
				if (auto* combo = dynamic_cast<ElemComboBox*>(observer.get()))
					combo->setSelectedIndex(static_cast<size_t>(_result - 1));
			});
	}

	void ElemComboBox::onMouseScroll(Rml::Event& _event)
	{
		const auto delta = helper::getMouseWheelDelta(_event).y;
		if (delta == 0.0f)
			return;

		const auto current = getSelectedIndex();

		if (current < 0)
			return;

		// the control takes the wheel, a view around it does not scroll as well
		_event.StopPropagation();

		// one entry per wheel notch: a trackpad or a smooth wheel sends many small
		// deltas per gesture, they add up until they make a notch. A turn the other
		// way starts over.
		if ((delta > 0.0f) != (m_wheelAccum > 0.0f))
			m_wheelAccum = 0.0f;
		m_wheelAccum += delta;
		if (std::abs(m_wheelAccum) < 1.0f)
			return;
		m_wheelAccum = std::fmod(m_wheelAccum, 1.0f);

		const auto offset = delta < 0.0f ? -1 : 1;

		auto next = current + offset;

		while (next >= 0 && static_cast<size_t>(next) < m_options.size() && m_options[static_cast<size_t>(next)].hidden)
			next += offset;

		if (next < 0 || static_cast<size_t>(next) >= m_options.size())
			return;

		setSelectedIndex(static_cast<size_t>(next));
	}

	void ElemComboBox::setSelectedIndex(const size_t _index, const bool _sendChangeEvent/* = true*/)
	{
		if (_index >= m_options.size())
			return;

		setValue(static_cast<float>(m_options[_index].value), _sendChangeEvent);

		if (!_sendChangeEvent)
			updateValueText();
	}

	int ElemComboBox::getSelectedIndex() const
	{
		return getIndexFromValue(static_cast<int>(getValue()));
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

	int ElemComboBox::getIndexFromValue(int _value) const
	{
		for (size_t i = 0; i < m_options.size(); ++i)
		{
			if (m_options[i].value == _value)
				return static_cast<int>(i);
		}
		return -1;
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

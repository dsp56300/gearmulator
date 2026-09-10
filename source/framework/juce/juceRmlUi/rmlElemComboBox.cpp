#include "rmlElemComboBox.h"

#include <algorithm>

#include "rmlHelper.h"
#include "rmlInterfaces.h"
#include "juceRmlComponent.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juceRmlUi
{
	namespace
	{
		// Styling for the popup window variant. juce's stock menu is sized for its own demo apps,
		// 17px text and roomy rows; this matches the compact dialogs the variant is used in.
		// popup="window" hands the list to a native juce popup instead of the in-document Menu.
		// That is deliberate: an RmlUi menu is clipped to its component, and the standalone
		// settings window is small enough that a long list - audio buffer sizes, MIDI ports - would
		// be cut off. Making Menu scroll would not change that.
		//
		// The price is this theme, which is NOT skinned: these colours are compiled in and one
		// instance is shared by every combo that asks for a window popup. Only the 88emu player
		// uses it today and they match its skin. A plugin adding popup="window" would get this
		// palette regardless of its own skin - read them from the element's computed values first
		// if that ever needs to work.
		class ComboPopupLookAndFeel final : public juce::LookAndFeel_V4
		{
		public:
			ComboPopupLookAndFeel()
			{
				setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff252b30));
				setColour(juce::PopupMenu::textColourId, juce::Colour(0xffdce2e6));
				setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff3a4650));
				setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffffff));
			}

			juce::Font getPopupMenuFont() override
			{
				return juce::Font(13.0f);
			}
		};

		// One instance serves every open combo box; it goes away with the last one.
		std::shared_ptr<juce::LookAndFeel> getPopupLookAndFeel()
		{
			static std::weak_ptr<juce::LookAndFeel> cache;
			auto laf = cache.lock();
			if (!laf)
			{
				laf = std::make_shared<ComboPopupLookAndFeel>();
				cache = laf;
			}
			return laf;
		}
	}

	ElemComboBox::ElemComboBox(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : ElemValue(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Click, this);
		AddEventListener(Rml::EventId::Mousescroll, this);
	}

	ElemComboBox::~ElemComboBox()
	{
		RemoveEventListener(Rml::EventId::Click, this);
		RemoveEventListener(Rml::EventId::Mousescroll, this);
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
			onClick(_event);
		}
		else if (_event.GetId() == Rml::EventId::Mousescroll)
		{
			onMouseScroll(_event);
		}
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

	void ElemComboBox::openPopupWindow()
	{
		auto* component = RmlComponent::fromElement(this);
		if (!component)
			return;

		if (!m_popupLookAndFeel)
			m_popupLookAndFeel = getPopupLookAndFeel();

		juce::PopupMenu menu;
		menu.setLookAndFeel(m_popupLookAndFeel.get());

		const auto currentValue = static_cast<int>(getValue());

		for (size_t i = 0; i < m_options.size(); ++i)
		{
			const auto& option = m_options[i];

			if (option.text.empty())
				continue;

			// item ids must be positive, 0 is "dismissed"
			menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(option.text.c_str()), true, option.value == currentValue);
		}

		// Element coordinates are in the context's pixels, i.e. the component's logical size times
		// the render scale; the popup wants screen coordinates in logical pixels.
		const auto scale = component->getOpenGLRenderingScale();
		const auto offset = GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto size = GetBox().GetSize(Rml::BoxArea::Border);
		const juce::Rectangle<int> local(
			juce::roundToInt(offset.x / scale), juce::roundToInt(offset.y / scale),
			juce::roundToInt(size.x / scale), juce::roundToInt(size.y / scale));
		const auto screenArea = component->localAreaToGlobal(local);

		const auto options = juce::PopupMenu::Options()
			.withTargetScreenArea(screenArea)
			.withMinimumWidth(screenArea.getWidth())
			.withStandardItemHeight(22)
			// one column, scrolling: a long list such as buffer sizes stays a list
			.withMaximumNumColumns(1)
			.withDeletionCheck(*component);

		// The document may be reloaded while the popup is up - a driver change rebuilds the
		// settings - so the element is observed, not captured. The look-and-feel is kept alive
		// here because the menu window uses it until it is dismissed.
		menu.showMenuAsync(options, [observer = Rml::Element::GetObserverPtr(GetCoreInstance()), lookAndFeel = m_popupLookAndFeel](const int _result)
		{
			if (_result <= 0 || !observer)
				return;
			auto* combo = dynamic_cast<ElemComboBox*>(observer.get());
			if (!combo)
				return;
			auto* comp = RmlComponent::fromElement(combo);
			if (!comp)
				return;
			RmlInterfaces::ScopedAccess access(*comp);
			combo->setSelectedIndex(static_cast<size_t>(_result - 1));
			comp->enqueueUpdate();
		});
	}

	void ElemComboBox::onMouseScroll(const Rml::Event& _event)
	{
		const auto offset = helper::isMouseWheelUp(_event) ? -1 : 1;

		const auto current = getSelectedIndex();

		if (current < 0)
			return;

		const auto next = current + offset;

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

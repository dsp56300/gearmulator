#include "uiObject.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>

#include "editor.h"

#include "rotaryStyle.h"
#include "comboboxStyle.h"
#include "buttonStyle.h"
#include "textbuttonStyle.h"
#include "hyperlinkbuttonStyle.h"
#include "labelStyle.h"

namespace genericUI
{
	UiObject::UiObject(const juce::var& _json)
	{
		auto* obj = _json.getDynamicObject();

		parse(obj);
	}

	UiObject::~UiObject()
	{
		m_juceObjects.clear();
		m_style.reset();
	}

	void UiObject::createJuceTree(Editor& _editor)
	{
		apply(_editor, _editor);

		createChildObjects(_editor, _editor);
	}

	void UiObject::createChildObjects(Editor& _editor, juce::Component& _parent) const
	{
		for (auto& ch : m_children)
		{
			auto* obj = ch->createJuceObject(_editor);

			if(!obj)
				continue;

			_parent.addAndMakeVisible(obj);

			ch->createChildObjects(_editor, *obj);
		}
	}

	void UiObject::createTabGroups(Editor& _editor)
	{
		if(m_tabGroup.isValid())
		{
			m_tabGroup.create(_editor);
			_editor.registerTabGroup(&m_tabGroup);
		}

		for (auto& ch : m_children)
		{
			ch->createTabGroups(_editor);
		}
	}

	void UiObject::apply(Editor& _editor, juce::Component& _target)
	{
		const auto x = getPropertyInt("x");
		const auto y = getPropertyInt("y");
		const auto w = getPropertyInt("width");
		const auto h = getPropertyInt("height");

		if(w < 1 || h < 1)
		{
			std::stringstream ss;
			ss << "Size " << w << "x" << h << " for object named " << m_name << " is invalid, each side must be > 0";
			throw std::runtime_error(ss.str());
		}

		_target.setTopLeftPosition(x, y);
		_target.setSize(w, h);

		createCondition(_editor, _target);
	}

	void UiObject::apply(Editor& _editor, juce::Slider& _target)
	{
	    _target.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

		apply(_editor, static_cast<juce::Component&>(_target));

		auto* const s = new RotaryStyle(_editor);

		createStyle(_editor, _target, s);

		const auto sliderStyle = s->getStyle();

	    switch (sliderStyle)
	    {
	    case RotaryStyle::Style::Rotary: 
			_target.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
			break;
	    case RotaryStyle::Style::LinearVertical:
			_target.setSliderStyle(juce::Slider::LinearVertical);
			break;
	    case RotaryStyle::Style::LinearHorizontal:
			_target.setSliderStyle(juce::Slider::LinearHorizontal);
			break;
	    }

		bindParameter(_editor, _target);
	}

	void UiObject::apply(Editor& _editor, juce::ComboBox& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		auto* s = new ComboboxStyle(_editor);
		createStyle(_editor, _target, s);
		bindParameter(_editor, _target);
		s->apply(_target);
	}

	void UiObject::apply(Editor& _editor, juce::DrawableButton& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		auto* s = new ButtonStyle(_editor);
		createStyle(_editor, _target, s);
		s->apply(_target);
		bindParameter(_editor, _target);
	}

	void UiObject::apply(Editor& _editor, juce::Label& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		auto* s = new LabelStyle(_editor);
		createStyle(_editor, _target, s);
		s->apply(_target);
	}

	void UiObject::apply(Editor& _editor, juce::TextButton& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		auto* s = new TextButtonStyle(_editor);
		createStyle(_editor, _target, s);
		s->apply(_target);
	}

	void UiObject::apply(Editor& _editor, juce::HyperlinkButton& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		auto* s = new HyperlinkButtonStyle(_editor);
		createStyle(_editor, _target, s);
		s->apply(_target);
	}

	void UiObject::collectVariants(std::set<std::string>& _dst, const std::string& _property) const
	{
		const std::string res = getProperty(_property);

		if (!res.empty())
			_dst.insert(res);

		for (const auto& child : m_children)
			child->collectVariants(_dst, _property);
	}

	juce::Component* UiObject::createJuceObject(Editor& _editor)
	{
		m_juceObjects.clear();

		if(hasComponent("rotary"))
		{
			createJuceObject<juce::Slider>(_editor);
		}
		else if(hasComponent("image"))
		{
			// @Juce: Juce does not respect translations on drawables by using setPositionAndSize, even though a Drawable is a component?! Doesn't make sense to me
			auto* c = createJuceObject<juce::Component>(_editor);
			auto img = _editor.createImageDrawable(getProperty("texture"));
			c->addAndMakeVisible(img.get());
			m_juceObjects.emplace_back(std::move(img));
		}
		else if(hasComponent("combobox"))
		{
			createJuceObject<juce::ComboBox>(_editor);
		}
		else if(hasComponent("button"))
		{
			createJuceObject(_editor, new juce::DrawableButton(m_name, juce::DrawableButton::ImageRaw));
		}
		else if(hasComponent("hyperlinkbutton"))
		{
			createJuceObject<juce::HyperlinkButton>(_editor);
		}
		else if(hasComponent("textbutton"))
		{
			createJuceObject<juce::TextButton>(_editor);
		}
		else if(hasComponent("label"))
		{
			createJuceObject<juce::Label>(_editor);
		}
		else if(hasComponent("root") || hasComponent("component"))
		{
			createJuceObject<juce::Component>(_editor);
		}
		else
		{
			throw std::runtime_error("Failed to determine object type for object named " + m_name);
		}

		return m_juceObjects.empty() ? nullptr : m_juceObjects.front().get();
	}

	int UiObject::getPropertyInt(const std::string& _key, int _default) const
	{
		const auto res = getProperty(_key);
		if (res.empty())
			return _default;
		return strtol(res.c_str(), nullptr, 10);
	}

	float UiObject::getPropertyFloat(const std::string& _key, float _default) const
	{
		const auto res = getProperty(_key);
		if (res.empty())
			return _default;
		return static_cast<float>(atof(res.c_str()));
	}

	std::string UiObject::getProperty(const std::string& _key, const std::string& _default) const
	{
		for (const auto& properties : m_components)
		{
			for(const auto& prop : properties.second)
			{
				if (prop.first == _key)
					return prop.second;
			}
		}
		return _default;
	}

	size_t UiObject::getConditionCountRecursive() const
	{
		size_t count = m_condition ? 1 : 0;

		for (const auto & c : m_children)
			count += c->getConditionCountRecursive();

		return count;
	}

	void UiObject::createCondition(Editor& _editor, juce::Component& _target)
	{
		if(!hasComponent("condition"))
			return;

		const auto paramName = getProperty("enableOnParameter");

		const auto index = _editor.getInterface().getParameterIndexByName(paramName);

		if(index < 0)
			throw std::runtime_error("Parameter named " + paramName + " not found");

		const auto v = _editor.getInterface().getParameterValue(index);

		if(!v)
			throw std::runtime_error("Parameter named " + paramName + " not found");

		const auto conditionValues = getProperty("enableOnValues");

		size_t start = 0;

		std::set<uint8_t> values;

		for(size_t i=0; i<=conditionValues.size(); ++i)
		{
			const auto isEnd = i == conditionValues.size() || conditionValues[i] == ',' || conditionValues[i] == ';';

			if(!isEnd)
				continue;

			const auto valueString = conditionValues.substr(start, i - start);
			const int val = strtol(valueString.c_str(), nullptr, 10);
			values.insert(static_cast<uint8_t>(val));

			start = i + 1;
		}

		m_condition.reset(new Condition(_target, *v, values));
	}

	bool UiObject::parse(juce::DynamicObject* _obj)
	{
		if (!_obj)
			return false;

		const auto& props = _obj->getProperties();

		for (int i = 0; i < props.size(); ++i)
		{
			const auto key = std::string(props.getName(i).toString().toUTF8());
			const auto value = props.getValueAt(i);

			if (key == "name")
			{
				m_name = value.toString().toStdString();
			}
			else if(key == "children")
			{
				const auto children = value.getArray();

				if (children)
				{
					for(int c=0; c<children->size(); ++c)
					{
						std::unique_ptr<UiObject> child;
						child.reset(new UiObject((*children)[c]));
						m_children.emplace_back(std::move(child));
					}
				}
			}
			else if(key == "tabgroup")
			{
				auto buttons = value["buttons"].getArray();
				auto pages = value["pages"].getArray();
				auto name = value["name"].toString().toStdString();

				if(name.empty())
					throw std::runtime_error("tab group needs to have a name");
				if(buttons == nullptr)
					throw std::runtime_error("tab group needs to define at least one button but 'buttons' array not found");
				if(pages == nullptr)
					throw std::runtime_error("tab group needs to define at least one page but 'pages' array not found");

				std::vector<std::string> buttonVec;
				std::vector<std::string> pagesVec;

				for (const auto& button : *buttons)
				{
					const auto b = button.toString().toStdString();
					if(b.empty())
						throw std::runtime_error("tab group button name must not be empty");
					buttonVec.push_back(b);
				}

				for (const auto& page : *pages)
				{
					const auto p = page.toString().toStdString();
					if(p.empty())
						throw std::runtime_error("tab group page name must not be empty");
					pagesVec.push_back(p);
				}

				if(buttonVec.size() != pagesVec.size())
					throw std::runtime_error("tab group page count must match tap group button count");

				m_tabGroup = TabGroup(name, pagesVec, buttonVec);
			}
			else
			{
				auto* componentDesc = value.getDynamicObject();

				if(componentDesc)
				{
					const auto& componentProps = componentDesc->getProperties();

					std::map<std::string, std::string> properties;

					for(int p=0; p<componentProps.size(); ++p)
					{
						properties.insert(std::make_pair(componentProps.getName(p).toString().toStdString(), componentProps.getValueAt(p).toString().toStdString()));
					}

					m_components.insert(std::make_pair(key, properties));
				}
			}
		}

		return true;
	}

	void UiObject::readProperties(juce::Component& _target)
	{
		const auto it = m_components.find("componentProperties");
		if(it == m_components.end())
			return;

		const auto props = it->second;

		for (const auto& prop : props)
			_target.getProperties().set(juce::Identifier(prop.first.c_str()), juce::var(prop.second.c_str()));
	}

	template <typename T> T* UiObject::createJuceObject(Editor& _editor)
	{
		return createJuceObject(_editor, new T());
	}

	template <typename T> T* UiObject::createJuceObject(Editor& _editor, T* _object)
	{
		std::unique_ptr<T> c(_object);
		apply(_editor, *c);
		auto* comp = c.get();
		m_juceObjects.emplace_back(std::move(c));

		if(!m_name.empty())
			_editor.registerComponent(m_name, comp);

		auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*>(_object);

		if(tooltipClient)
		{
			const auto tooltip = getProperty("tooltip");

			if(!tooltip.empty())
				tooltipClient->setTooltip(tooltip);
		}

		readProperties(*_object);

		return comp;
	}

	template<typename T>
	void UiObject::bindParameter(const Editor& _editor, T& _target) const
	{
		const std::string param = getProperty("parameter");

		if(param.empty())
			return;

		const auto index = _editor.getInterface().getParameterIndexByName(param);

		if(index < 0)
			throw std::runtime_error("Parameter named " + param + " not found");

		_editor.getInterface().bindParameter(_target, index);

		_target.getProperties().set("parameter", index);
	}

	template <typename Target, typename Style> void UiObject::createStyle(Editor& _editor, Target& _target, Style* _style)
	{
		m_style.reset(_style);
		m_style->apply(_editor, *this);
		_target.setLookAndFeel(m_style.get());
	}
}

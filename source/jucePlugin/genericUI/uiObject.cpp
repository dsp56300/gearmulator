#include "uiObject.h"

#include <cassert>
#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>

#include "editor.h"

#include "rotaryStyle.h"
#include "comboboxStyle.h"
#include "buttonStyle.h"

#include "../VirusController.h"
#include "../VirusParameterBinding.h"

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

	void UiObject::createJuceTree(Editor& _editor) const
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

	void UiObject::apply(Editor& _editor, juce::Component& _target) const
	{
		const auto x = getPropertyInt("x");
		const auto y = getPropertyInt("y");
		const auto w = getPropertyInt("width");
		const auto h = getPropertyInt("height");

		assert(w > 0 && h > 0);

		_target.setTopLeftPosition(x, y);
		_target.setSize(w, h);
	}

	void UiObject::apply(Editor& _editor, juce::Slider& _target)
	{
		_target.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
	    _target.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

		apply(_editor, static_cast<juce::Component&>(_target));

		createStyle(_editor, _target, new RotaryStyle());

		bindParameter(_editor, _target);
	}

	void UiObject::apply(Editor& _editor, juce::ComboBox& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		createStyle(_editor, _target, new ComboboxStyle());
		bindParameter(_editor, _target);
	}

	void UiObject::apply(Editor& _editor, juce::DrawableButton& _target)
	{
		apply(_editor, static_cast<juce::Component&>(_target));
		auto* s = new ButtonStyle();
		createStyle(_editor, _target, s);
		s->apply(_target);
		bindParameter(_editor, _target);
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

		if(hasComponent("root"))
		{
			auto c = std::make_unique<juce::Component>();
			apply(_editor, *c);
			m_juceObjects.emplace_back(std::move(c));
		}
		else if(hasComponent("rotary"))
		{
			auto c = std::make_unique<juce::Slider>();
			apply(_editor, *c);
			m_juceObjects.emplace_back(std::move(c));
		}
		else if(hasComponent("image"))
		{
			auto c = std::make_unique<juce::Component>();
			apply(_editor, *c);
			auto img = _editor.createImageDrawable(getProperty("texture"));
			c->addAndMakeVisible(img.get());
			m_juceObjects.emplace_back(std::move(c));
			m_juceObjects.emplace_back(std::move(img));
		}
		else if(hasComponent("combobox"))
		{
			auto c = std::make_unique<juce::ComboBox>();
			apply(_editor, *c);
			m_juceObjects.emplace_back(std::move(c));
		}
		else if(hasComponent("button"))
		{
			auto c = std::make_unique<juce::DrawableButton>(m_name, juce::DrawableButton::ImageRaw);
			apply(_editor, *c);
			m_juceObjects.emplace_back(std::move(c));
		}
		else
			assert(false && "unknown object type");

		return m_juceObjects.empty() ? nullptr : m_juceObjects.front().get();
	}

	int UiObject::getPropertyInt(const std::string& _key, int _default) const
	{
		const auto res = getProperty(_key);
		if (res.empty())
			return _default;
		return strtol(res.c_str(), nullptr, 10);
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

	bool UiObject::parse(juce::DynamicObject* _obj)
	{
		if (!_obj)
			return false;

		m_name = _obj->getProperty("name").toString().toStdString();

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

	template<typename T>
	void UiObject::bindParameter(const Editor& _editor, T& _target) const
	{
		const std::string param = getProperty("parameter");

		if(param.empty())
			return;

		const auto index = _editor.getController().getParameterTypeByName(param);
		assert(index >= 0 && "parameter type not found");

		auto& binding = _editor.getParameterBinding();

		binding.bind(_target, index);
	}

	template <typename Target, typename Style> void UiObject::createStyle(Editor& _editor, Target& _target, Style* _style)
	{
		m_style.reset(_style);
		m_style->apply(_editor, *this);
		_target.setLookAndFeel(m_style.get());
	}
}

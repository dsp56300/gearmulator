#include "uiObject.h"

#include <utility>

#include "editor.h"

#include "comboboxStyle.h"

#include <cassert>

#include "button.h"

namespace genericUI
{
	UiObject::UiObject(UiObject* _parent, const juce::var& _json, const bool _isTemplate/* = false*/) : m_isTemplate(_isTemplate), m_parent(_parent)
	{
		auto* obj = _json.getDynamicObject();

		parse(obj);
	}

	UiObject::~UiObject()
	{
		m_juceObjects.clear();
		m_style.reset();
	}

	void UiObject::collectVariants(std::set<std::string>& _dst, const std::string& _property) const
	{
		const std::string res = getProperty(_property);

		if (!res.empty())
			_dst.insert(res);

		for (const auto& child : m_children)
			child->collectVariants(_dst, _property);

		for (const auto& child : m_templates)
			child->collectVariants(_dst, _property);
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

	bool UiObject::hasComponent(const std::string& _component) const
	{
		return m_components.find(_component) != m_components.end();
	}

	std::set<std::string> UiObject::readConditionValues() const
	{
		std::set<std::string> values;

		const auto conditionValues = getProperty("enableOnValues");
		size_t start = 0;

		for(size_t i=0; i<=conditionValues.size(); ++i)
		{
			const auto isEnd = i == conditionValues.size() || conditionValues[i] == ',' || conditionValues[i] == ';';

			if(!isEnd)
				continue;

			const auto valueString = conditionValues.substr(start, i - start);

			values.insert(valueString);

			start = i + 1;
		}

		return values;
	}

	const std::map<std::string, std::string>& UiObject::getComponentProperties() const
	{
		const auto it = m_components.find("componentProperties");
		if(it == m_components.end())
		{
			static std::map<std::string, std::string> emptyProps;
			return emptyProps;
		}

		return it->second;
	}

	juce::DynamicObject& UiObject::applyStyle(juce::DynamicObject& _obj, const std::string& _styleName)
	{
		if (m_parent)
			m_parent->applyStyle(_obj, _styleName);

		const auto it = m_styles.find(_styleName);
		if (it == m_styles.end())
			return _obj;

		const auto& s = it->second;
		const auto sourceObj = s.getDynamicObject();

		if (sourceObj)
			copyPropertiesRecursive(_obj, *sourceObj);

		return _obj;
	}

	bool UiObject::copyPropertiesRecursive(juce::DynamicObject& _target, const juce::DynamicObject& _source)
	{
		bool result = false;

		for (const auto& sourceProp : _source.getProperties())
		{
			const auto& key = sourceProp.name;
			const auto& val = sourceProp.value;

			if (!_target.hasProperty(key))
			{
				_target.setProperty(key, val);
				result = true;
			}
			else
			{
				auto& targetProperty = _target.getProperty(key);

				if (targetProperty.isObject())
				{
					auto targetObj = targetProperty.getDynamicObject();
					if (targetObj)
					{
						auto sourceObj = val.getDynamicObject();
						if (sourceObj)
							copyPropertiesRecursive(*targetObj, *sourceObj);
					}
				}
			}
		}
		return true;
	}

	bool UiObject::parse(juce::DynamicObject* _obj)
	{
		if (!_obj)
			return false;

		auto props = _obj->getProperties();

		auto styleName = props["style"].toString().toStdString();

		std::unique_ptr<juce::DynamicObject> newObj;

		if (!styleName.empty())
		{
			newObj = _obj->clone();
			props = applyStyle(*newObj, styleName).getProperties();
		}

		for (int i = 0; i < props.size(); ++i)
		{
			const auto key = std::string(props.getName(i).toString().toUTF8());
			const auto& value = props.getValueAt(i);

			if (key == "name")
			{
				m_name = value.toString().toStdString();
			}
			else if(key == "children")
			{
				const auto children = value.getArray();

				if (children)
				{
					for (auto&& c : *children)
					{
						std::unique_ptr<UiObject> child;
						child.reset(new UiObject(this, c));
						m_children.emplace_back(std::move(child));
					}
				}
			}
			else if (key == "templates")
			{
				if (const auto children = value.getArray())
				{
					for (const auto& c : *children)
						m_templates.emplace_back(std::make_shared<UiObject>(this, c, true));
				}
			}
			else if (key == "styles")
			{
				auto obj = value.getDynamicObject();
				if (!obj)
					throw std::runtime_error("styles must be an object");

				auto p = obj->getProperties();

				for (const auto& s : p)
				{
					const auto name = s.name.toString().toStdString();

					if (name.empty())
						throw std::runtime_error("style needs to have a name");
					if (m_styles.find(name) != m_styles.end())
						throw std::runtime_error("style with name " + name + " already exists");

					m_styles.insert({name, s.value});
				}

				for (const auto& [name,s] : m_styles)
				{
					if (auto* styleObj = s.getDynamicObject())
					{
						auto parentStyleName = styleObj->getProperty("style").toString().toStdString();

						if (!parentStyleName.empty())
						{
							applyStyle(*styleObj, parentStyleName);
						}
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
			else if(key == "controllerlinks")
			{
				auto* entries = value.getArray();

				if(entries && !entries->isEmpty())
				{
					for(auto j=0; j<entries->size(); ++j)
					{
						const auto& e = (*entries)[j];
						const auto source = e["source"].toString().toStdString();
						const auto dest = e["dest"].toString().toStdString();
						const auto condition = e["condition"].toString().toStdString();

						if(source.empty())
							throw std::runtime_error("source for controller link needs to have a name");
						if(dest.empty())
							throw std::runtime_error("destination for controller link needs to have a name");
						if(condition.empty())
							throw std::runtime_error("condition for controller link needs to have a name");

						m_controllerLinks.emplace_back(new ControllerLink(source, dest, condition));
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

	void UiObject::readProperties(juce::Component& _target)
	{
		const auto it = m_components.find("componentProperties");
		if(it == m_components.end())
			return;

		const auto props = it->second;

		for (const auto& prop : props)
			_target.getProperties().set(juce::Identifier(prop.first.c_str()), juce::var(prop.second.c_str()));
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

		_target.getProperties().set("parameter", index);
		_target.getProperties().set("parametername", juce::String(param));
		
		if constexpr(std::is_base_of_v<juce::Button, T>)
		{
			auto value = getPropertyInt("value", -1);
			if(value != -1)
				_target.getProperties().set("parametervalue", value);
			auto valueOff = getPropertyInt("valueOff", -1);
			if(valueOff != -1)
				_target.getProperties().set("parametervalueoff", valueOff);
		}

		_editor.getInterface().bindParameter(_target, index);
	}

	template <typename Target, typename Style> void UiObject::createStyle(Editor& _editor, Target& _target, Style* _style)
	{
		if(_style)
			m_style.reset(_style);
		m_style->apply(_editor, *this);
		_target.setLookAndFeel(m_style.get());
	}
}

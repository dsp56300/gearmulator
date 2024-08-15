#include "parameterbinding.h"

#include <cassert>

#include "parameter.h"
#include "controller.h"

namespace pluginLib
{
	ParameterBinding::MouseListener::MouseListener(Parameter* _param, juce::Slider& _slider)
	: m_param(_param), m_slider(&_slider)
	{
	}

	void ParameterBinding::MouseListener::mouseDown(const juce::MouseEvent& event)
	{
		m_param->pushChangeGesture();
	}

	void ParameterBinding::MouseListener::mouseUp(const juce::MouseEvent& event)
	{
		m_param->popChangeGesture();
	}

	void ParameterBinding::MouseListener::mouseDrag(const juce::MouseEvent& event)
	{
		m_param->setUnnormalizedValueNotifyingHost(static_cast<float>(m_slider->getValue()), Parameter::Origin::Ui);
	}

	void ParameterBinding::MouseListener::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
	{
		m_param->setUnnormalizedValueNotifyingHost(static_cast<float>(m_slider->getValue()), Parameter::Origin::Ui);
	}

	void ParameterBinding::MouseListener::mouseDoubleClick(const juce::MouseEvent& event)
	{
		m_param->setValueNotifyingHost(m_param->getDefaultValue(), Parameter::Origin::Ui);
	}

	ParameterBinding::~ParameterBinding()
	{
		clearBindings();
	}

	void ParameterBinding::bind(juce::Slider &_slider, uint32_t _param)
	{
		bind(_slider, _param, CurrentPart);
	}
	void ParameterBinding::bind(juce::Slider &_slider, uint32_t _param, const uint8_t _part)
	{
		const auto v = m_controller.getParameter(_param, _part == CurrentPart ? m_controller.getCurrentPart() : _part);

		if (!v)
		{
			assert(false && "Failed to find parameter");
			return;
		}

		removeMouseListener(_slider);

		auto* listener = new MouseListener(v, _slider);
		m_sliderMouseListeners.insert(std::make_pair(&_slider, listener));

		_slider.addMouseListener(listener, false);

		const auto& range = v->getNormalisableRange();

		_slider.setRange(range.start, range.end, range.interval);
		_slider.setDoubleClickReturnValue(true, v->convertFrom0to1(v->getDefaultValue()));
		_slider.getValueObject().referTo(v->getValueObject());
		_slider.getProperties().set("type", "slider");
		_slider.getProperties().set("name", juce::String(v->getDescription().name));

		if (v->isBipolar()) 
			_slider.getProperties().set("bipolar", true);

		// Juce bug: If the range changes but the value doesn't, Juce doesn't issue a repaint. Do it manually
		_slider.repaint();

		const BoundParameter p{v, &_slider, _param, _part};
		addBinding(p);
	}

	void ParameterBinding::bind(juce::ComboBox& _combo, uint32_t _param)
	{
		bind(_combo, _param, CurrentPart);
	}

	void ParameterBinding::bind(juce::ComboBox& _combo, const uint32_t _param, uint8_t _part)
	{
		const auto v = m_controller.getParameter(_param, _part == CurrentPart ? m_controller.getCurrentPart() : _part);

		if (!v)
		{
			assert(false && "Failed to find parameter");
			return;
		}

		_combo.setTextWhenNothingSelected("-");
		_combo.setScrollWheelEnabled(true);

		_combo.onChange = nullptr;
		_combo.clear();

		using Entry = std::pair<uint8_t, std::string>;

		std::vector<Entry> sortedValues;

		const auto& desc = v->getDescription();
		const auto& valueList = desc.valueList;
		const auto& range = desc.range;

		if(valueList.order.empty())
		{
			uint8_t i = 0;
			const auto& allValues = v->getAllValueStrings();
			for (const auto& vs : allValues)
			{
				if(vs.isNotEmpty() && i >= range.getStart() && i <= range.getEnd())
					sortedValues.emplace_back(i, vs.toStdString());
				++i;
			}
		}
		else
		{
			for(uint32_t i=0; i<valueList.order.size(); ++i)
			{
				const auto value = valueList.orderToValue(i);
				if(value == ValueList::InvalidIndex)
					continue;
				if(i < range.getStart() || i > range.getEnd())
					continue;
				const auto text = valueList.valueToText(value);
				if(text.empty())
					continue;
				sortedValues.emplace_back(value, text);
			}
		}
	
		uint32_t count = 0;

		// we want our long menus to be split into columns of 16 rows each
		// but only if we have more entries than one and a half such column
		const uint32_t splitAt = (sortedValues.size() > 24) ? 16 : 0;

		for (const auto &vs : sortedValues)
		{
			_combo.addItem(vs.second, vs.first + 1);

			if (++count == splitAt)
			{
				_combo.getRootMenu()->addColumnBreak();
				count = 0;
			}
		}

		_combo.setSelectedId(static_cast<int>(v->getValueObject().getValueSource().getValue()) + 1, juce::dontSendNotification);

		_combo.onChange = [this, &_combo, v]()
		{
			const auto id = _combo.getSelectedId();

			if(id == 0)
				return;

			const int current = v->getValueObject().getValueSource().getValue();

			if((id - 1) == current)
				return;

			if(v->getDescription().isPublic)
			{
				v->setUnnormalizedValueNotifyingHost(id - 1, Parameter::Origin::Ui);
			}
			else
			{
				v->setUnnormalizedValue(id - 1, Parameter::Origin::Ui);
			}
		};

		const auto listenerId = v->onValueChanged.addListener([this, &_combo](pluginLib::Parameter* v)
		{
			const auto value = v->getUnnormalizedValue();
			_combo.setSelectedId(value + 1, juce::dontSendNotification);
		});

		const BoundParameter p{v, &_combo, _param, _part, listenerId};
		addBinding(p);
	}

	void ParameterBinding::bind(juce::Button &_btn, const uint32_t _param)
	{
		bind(_btn, _param, CurrentPart);
	}

	void ParameterBinding::bind(juce::Button& _control, const uint32_t _param, const uint8_t _part)
	{
		const auto param = m_controller.getParameter(_param, _part == CurrentPart ? m_controller.getCurrentPart() : _part);
		if (!param)
		{
			assert(false && "Failed to find parameter");
			return;
		}

		const bool hasCustomValue = _control.getProperties().contains("parametervalue");
		int paramValue = 1;
		int paramValueOff = -1;
		if(hasCustomValue)
			paramValue = _control.getProperties()["parametervalue"];
		if(_control.getProperties().contains("parametervalueoff"))
			paramValueOff = _control.getProperties()["parametervalueoff"];

		_control.onClick = [&_control, param, paramValue, hasCustomValue, paramValueOff]
		{
			const auto on = _control.getToggleState();
			if(hasCustomValue)
			{
				if(on)
					param->setUnnormalizedValueNotifyingHost(paramValue, Parameter::Origin::Ui);
				else if(paramValueOff != -1)
					param->setUnnormalizedValueNotifyingHost(paramValueOff, Parameter::Origin::Ui);
				else
					_control.setToggleState(true, juce::dontSendNotification);
			}
			else
			{
				param->setUnnormalizedValueNotifyingHost(on ? 1 : 0, Parameter::Origin::Ui);
			}
		};

		_control.setToggleState(param->getUnnormalizedValue() == paramValue, juce::dontSendNotification);

		const auto listenerId = param->onValueChanged.addListener([this, &_control, paramValue](const Parameter* _p)
		{
			const auto value = _p->getUnnormalizedValue();
			_control.setToggleState(value == paramValue, juce::dontSendNotification);
		});

		const BoundParameter p{param, &_control, _param, CurrentPart, listenerId};
		addBinding(p);
	}

	bool ParameterBinding::bind(juce::Component& _component, const uint32_t _param, const uint8_t _part)
	{
		if(auto* slider = dynamic_cast<juce::Slider*>(&_component))
		{
			bind(*slider, _param, _part);
			return true;
		}
		if(auto* button = dynamic_cast<juce::DrawableButton*>(&_component))
		{
			bind(*button, _param, _part);
			return true;
		}
		if(auto* comboBox = dynamic_cast<juce::ComboBox*>(&_component))
		{
			bind(*comboBox, _param, _part);
			return true;
		}
		assert(false && "unknown component type");
		return false;
	}

	juce::Component* ParameterBinding::getBoundComponent(const Parameter* _parameter) const
	{
		const auto it = m_boundParameters.find(_parameter);
		if(it == m_boundParameters.end())
			return nullptr;
		return it->second;
	}

	Parameter* ParameterBinding::getBoundParameter(const juce::Component* _component) const
	{
		const auto it = m_boundComponents.find(_component);
		if(it == m_boundComponents.end())
			return nullptr;
		return it->second;
	}

	bool ParameterBinding::unbind(const Parameter* _param)
	{
		for (auto it= m_bindings.begin(); it != m_bindings.end(); ++it)
		{
			if(it->parameter != _param)
				continue;

			m_bindings.erase(it);

			return true;
		}
		return false;
	}

	bool ParameterBinding::unbind(const juce::Component* _component)
	{
		for (auto it= m_bindings.begin(); it != m_bindings.end(); ++it)
		{
			if(it->component != _component)
				continue;

			disableBinding(*it);

			m_disabledBindings.push_back(*it);
			m_bindings.erase(it);

			return true;
		}
		return false;
	}

	void ParameterBinding::removeMouseListener(juce::Slider& _slider)
	{
		const auto it = m_sliderMouseListeners.find(&_slider);

		if(it != m_sliderMouseListeners.end())
		{
			_slider.removeMouseListener(it->second);
			delete it->second;
			m_sliderMouseListeners.erase(it);
		}
	}

	void ParameterBinding::bind(const std::vector<BoundParameter>& _bindings, const bool _currentPartOnly)
	{
		for (const auto& b : _bindings)
			bind(*b.component, b.paramIndex, b.part);
	}

	void ParameterBinding::addBinding(const BoundParameter& _boundParameter)
	{
		m_bindings.emplace_back(_boundParameter);

		m_boundComponents.erase(_boundParameter.component);
		m_boundParameters.erase(_boundParameter.parameter);

		m_boundParameters.insert(std::make_pair(_boundParameter.parameter, _boundParameter.component));
		m_boundComponents.insert(std::make_pair(_boundParameter.component, _boundParameter.parameter));

		onBind(_boundParameter);
	}

	void ParameterBinding::disableBinding(BoundParameter& _b)
	{
		onUnbind(_b);

		m_boundParameters.erase(_b.parameter);
		m_boundComponents.erase(_b.component);

		auto* slider = dynamic_cast<juce::Slider*>(_b.component);

		if(slider != nullptr)
		{
			removeMouseListener(*slider);
			slider->getValueObject().referTo(juce::Value());
		}

		auto* combo = dynamic_cast<juce::ComboBox*>(_b.component);
		if(combo != nullptr)
			combo->onChange = nullptr;

		auto* button = dynamic_cast<juce::Button*>(_b.component);
		if(button != nullptr)
			button->onClick = nullptr;

		if(_b.onChangeListenerId != ParameterListener::InvalidListenerId)
		{
			_b.parameter->onValueChanged.removeListener(_b.onChangeListenerId);
			_b.onChangeListenerId = ParameterListener::InvalidListenerId;
		}
	}

	void ParameterBinding::clearBindings()
	{
		for (auto& b : m_bindings)
			disableBinding(b);
		clear();
	}

	void ParameterBinding::clear()
	{
		m_bindings.clear();
		m_boundParameters.clear();
		m_boundComponents.clear();
	}

	void ParameterBinding::setPart(uint8_t _part)
	{
		const std::vector<BoundParameter> bindings = m_bindings;

		clearBindings();

		m_controller.setCurrentPart(_part);

		bind(bindings, true);
	}

	void ParameterBinding::disableBindings()
	{
		m_disabledBindings.clear();
		std::swap(m_bindings, m_disabledBindings);

		for (auto& b : m_disabledBindings)
			disableBinding(b);
	}

	void ParameterBinding::enableBindings()
	{
		bind(m_disabledBindings, false);
		m_disabledBindings.clear();
	}
}

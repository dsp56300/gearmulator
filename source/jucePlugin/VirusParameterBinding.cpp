#include "VirusParameterBinding.h"

#include "PluginProcessor.h"

class Parameter;

VirusParameterBinding::~VirusParameterBinding()
{
	clearBindings();
}

void VirusParameterBinding::bind(juce::Slider &_slider, uint32_t _param)
{
	bind(_slider, _param, CurrentPart);
}
void VirusParameterBinding::bind(juce::Slider &_slider, uint32_t _param, const uint8_t _part)
{
	const auto v = m_controller.getParameter(_param, _part == CurrentPart ? m_controller.getCurrentPart() : _part);

	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}

	removeMouseListener(_slider);

	auto* listener = new VirusParameterBindingMouseListener(v, _slider);
	m_sliderMouseListeners.insert(std::make_pair(&_slider, listener));

	_slider.addMouseListener(listener, false);
	const auto range = v->getNormalisableRange();
	_slider.setRange(range.start, range.end, range.interval);
	_slider.setDoubleClickReturnValue(true, v->convertFrom0to1(v->getDefaultValue()));
	_slider.getValueObject().referTo(v->getValueObject());
	_slider.getProperties().set("type", "slider");
	_slider.getProperties().set("name", juce::String(v->getDescription().name));
	if (v->isBipolar()) {
		_slider.getProperties().set("bipolar", true);
	}
	const BoundParameter p{v, &_slider, _param, _part};
	addBinding(p);
}

void VirusParameterBinding::bind(juce::ComboBox& _combo, uint32_t _param)
{
	bind(_combo, _param, CurrentPart);
}

void VirusParameterBinding::bind(juce::ComboBox& _combo, const uint32_t _param, uint8_t _part)
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

	int idx = 1;
	uint32_t count = 0;
	for (const auto& vs : v->getAllValueStrings()) {
		if(vs.isNotEmpty())
		{
			_combo.addItem(vs, idx);
			if(++count == 16)
			{
				_combo.getRootMenu()->addColumnBreak();
				count = 0;
			}
		}
		idx++;
	}
	//_combo.addItemList(v->getAllValueStrings(), 1);
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
			v->beginChangeGesture();
			v->setValueNotifyingHost(v->convertTo0to1(static_cast<float>(id - 1)));
			v->endChangeGesture();
		}
		v->getValueObject().setValue(id - 1);
	};

	const auto listenerId = m_nextListenerId++;

	v->onValueChanged.emplace_back(std::make_pair(listenerId, [this, &_combo, v]()
	{
		const auto value = static_cast<int>(v->getValueObject().getValueSource().getValue());
		_combo.setSelectedId(value + 1, juce::dontSendNotification);
	}));

	const BoundParameter p{v, &_combo, _param, _part, listenerId};
	addBinding(p);
}

void VirusParameterBinding::bind(juce::Button &_btn, const uint32_t _param)
{
	const auto v = m_controller.getParameter(_param, m_controller.getCurrentPart());
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	_btn.getToggleStateValue().referTo(v->getValueObject());
	const BoundParameter p{v, &_btn, _param, CurrentPart};
	addBinding(p);
}

juce::Component* VirusParameterBinding::getBoundComponent(const pluginLib::Parameter* _parameter)
{
	const auto it = m_boundParameters.find(_parameter);
	if(it == m_boundParameters.end())
		return nullptr;
	return it->second;
}

void VirusParameterBinding::removeMouseListener(juce::Slider& _slider)
{
	auto it = m_sliderMouseListeners.find(&_slider);

	if(it != m_sliderMouseListeners.end())
	{
		_slider.removeMouseListener(it->second);
		delete it->second;
		m_sliderMouseListeners.erase(it);
	}
}

void VirusParameterBinding::bind(const std::vector<BoundParameter>& _bindings, const bool _currentPartOnly)
{
	for (const auto& b : _bindings)
	{
		if(_currentPartOnly && b.part != CurrentPart)
		{
			addBinding(b);
			continue;
		}

		const auto& desc = b.parameter->getDescription();
		const bool isNonPartExclusive = desc.isNonPartSensitive();

		if(isNonPartExclusive)
			continue;

		auto* slider = dynamic_cast<juce::Slider*>(b.component);
		if(slider)
		{
			bind(*slider, b.type);
			continue;
		}
		auto* button = dynamic_cast<juce::DrawableButton*>(b.component);
		if(button)
		{
			bind(*button, b.type);
			continue;
		}
		auto* comboBox = dynamic_cast<juce::ComboBox*>(b.component);
		if(comboBox)
		{
			bind(*comboBox, b.type);
			continue;
		}
		assert(false && "unknown component type");
	}
}

void VirusParameterBinding::addBinding(const BoundParameter& _boundParameter)
{
	m_bindings.emplace_back(_boundParameter);

	m_boundComponents.erase(_boundParameter.component);
	m_boundParameters.erase(_boundParameter.parameter);

	m_boundParameters.insert(std::make_pair(_boundParameter.parameter, _boundParameter.component));
	m_boundComponents.insert(std::make_pair(_boundParameter.component, _boundParameter.parameter));
}

void VirusParameterBinding::disableBinding(const BoundParameter& _b)
{
	m_boundParameters.erase(_b.parameter);
	m_boundComponents.erase(_b.component);

	auto* slider = dynamic_cast<juce::Slider*>(_b.component);

	if(slider != nullptr)
		removeMouseListener(*slider);

	auto* combo = dynamic_cast<juce::ComboBox*>(_b.component);
	if(combo != nullptr)
		combo->onChange = nullptr;

	if(_b.onChangeListenerId)
		_b.parameter->removeListener(_b.onChangeListenerId);
}

void VirusParameterBinding::clearBindings()
{
	for (const auto& b : m_bindings)
		disableBinding(b);

	m_bindings.clear();
	m_boundParameters.clear();
	m_boundComponents.clear();
}

void VirusParameterBinding::setPart(uint8_t _part)
{
	const std::vector<BoundParameter> bindings = m_bindings;

	clearBindings();

	m_controller.setCurrentPart(_part);

	bind(bindings, true);
}

void VirusParameterBinding::disableBindings()
{
	m_disabledBindings.clear();
	std::swap(m_bindings, m_disabledBindings);

	for (const auto& b : m_disabledBindings)
		disableBinding(b);
}

void VirusParameterBinding::enableBindings()
{
	bind(m_disabledBindings, false);
	m_disabledBindings.clear();
}

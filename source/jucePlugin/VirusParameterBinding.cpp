#include "VirusParameterBinding.h"
#include "VirusParameter.h"
#include "PluginProcessor.h"

class Parameter;

VirusParameterBinding::~VirusParameterBinding()
{
	clearBindings();
}

void VirusParameterBinding::clearBindings()
{
	for (const auto b : m_bindings)
		b.parameter->onValueChanged = nullptr;
	m_bindings.clear();
}

void VirusParameterBinding::setPart(uint8_t _part)
{
	m_processor.getController().setCurrentPart(_part);

	std::vector<BoundParameter> bindings;
	bindings.swap(m_bindings);

	for(size_t i=0; i<bindings.size(); ++i)
	{
		auto& b = bindings[i];

		if(b.part != CurrentPart)
		{
			m_bindings.push_back(b);
			continue;
		}

		const auto& desc = b.parameter->getDescription();
		const bool isNonPartExclusive = (desc.classFlags & Virus::Parameter::Class::GLOBAL) || (desc.classFlags & Virus::Parameter::Class::NON_PART_SENSITIVE);

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
void VirusParameterBinding::bind(juce::Slider &_slider, Virus::ParameterType _param)
{
	bind(_slider, _param, CurrentPart);
}
void VirusParameterBinding::bind(juce::Slider &_slider, Virus::ParameterType _param, const uint8_t _part)
{
	const auto v = m_processor.getController().getParameter(_param, _part == CurrentPart ? m_processor.getController().getCurrentPart() : _part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	_slider.addMouseListener(new VirusParameterBindingMouseListener(v, _slider), false);
	const auto range = v->getNormalisableRange();
	_slider.setRange(range.start, range.end, range.interval);
	_slider.setDoubleClickReturnValue(true, v->convertFrom0to1(v->getDefaultValue()));
	_slider.getValueObject().referTo(v->getValueObject());
	_slider.getProperties().set("type", "slider");
	_slider.getProperties().set("name", v->getDescription().name);
	if (v->isBipolar()) {
		_slider.getProperties().set("bipolar", true);
	}
	const BoundParameter p{v, &_slider, _param, _part};
	m_bindings.emplace_back(p);
}

void VirusParameterBinding::bind(juce::ComboBox& _combo, Virus::ParameterType _param)
{
	bind(_combo, _param, CurrentPart);
}

void VirusParameterBinding::bind(juce::ComboBox& _combo, const Virus::ParameterType _param, uint8_t _part)
{
	const auto v = m_processor.getController().getParameter(_param, _part == CurrentPart ? m_processor.getController().getCurrentPart() : _part);
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
	for (const auto& vs : v->getAllValueStrings()) {
		if(vs.isNotEmpty())
			_combo.addItem(vs, idx);
		idx++;
	}
	//_combo.addItemList(v->getAllValueStrings(), 1);
	_combo.setSelectedId(static_cast<int>(v->getValueObject().getValueSource().getValue()) + 1, juce::dontSendNotification);
	_combo.onChange = [this, &_combo, v]() {
		if(v->getDescription().isPublic)
		{
			v->beginChangeGesture();
			v->setValueNotifyingHost(v->convertTo0to1(static_cast<float>(_combo.getSelectedId() - 1)));
			v->endChangeGesture();
		}
		v->getValueObject().getValueSource().setValue((int)_combo.getSelectedId() - 1);
	};
	v->onValueChanged = [this, &_combo, v]() { _combo.setSelectedId(static_cast<int>(v->getValueObject().getValueSource().getValue()) + 1, juce::dontSendNotification); };
	const BoundParameter p{v, &_combo, _param, _part};
	m_bindings.emplace_back(p);
}

void VirusParameterBinding::bind(juce::Button &_btn, const Virus::ParameterType _param)
{
	const auto v = m_processor.getController().getParameter(_param, m_processor.getController().getCurrentPart());
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	_btn.getToggleStateValue().referTo(v->getValueObject());
	const BoundParameter p{v, &_btn, _param, CurrentPart};
	m_bindings.emplace_back(p);
}

#include "n2xVmMap.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	constexpr const char* g_postfix = "Sens";

	class SliderListener : public juce::Slider::Listener
	{
	public:
		SliderListener() = delete;

		SliderListener(const SliderListener&) = delete;
		SliderListener(SliderListener&&) = default;

		SliderListener& operator=(const SliderListener&) = delete;
		SliderListener& operator=(SliderListener&&) = default;

		SliderListener(juce::Slider* _slider, const std::function<void(juce::Slider*)>& _onValueChanged) : m_slider(_slider), m_onValueChanged(_onValueChanged)
		{
			_slider->addListener(this);
		}
		~SliderListener() override
		{
			m_slider->removeListener(this);
		}
		void sliderValueChanged(juce::Slider* _slider) override
		{
			if (m_onValueChanged)
				m_onValueChanged(_slider);
		}
	private:
		juce::Slider* m_slider;
		std::function<void(juce::Slider*)> m_onValueChanged;
	};

	VmMap::VmMap(Editor& _editor, pluginLib::ParameterBinding& _binding)
	: m_editor(_editor)
	, m_btVmMap(_editor.findComponentT<juce::Button>("VMMAP"))
	{
		auto& c = _editor.getN2xController();

		const auto& descs = c.getParameterDescriptions().getDescriptions();

		for (const auto& desc : descs)
		{
			uint32_t idxBase, idxVm;

			const auto& nameBase = desc.name;
			const auto& nameVm = desc.name + g_postfix;

			if(c.getParameterDescriptions().getIndexByName(idxBase, nameBase) &&
				c.getParameterDescriptions().getIndexByName(idxVm, nameVm))
			{
				auto* compBase = _editor.findComponentByParamT<juce::Slider>(nameBase);
				auto* compVm = _editor.findComponentByParamT<juce::Slider>(nameVm);

				// we do not want vm params to be bound at all, we do this manually. Remove any binding that might still be present in a skin
				_binding.unbind(compVm);

				auto vmParam = std::make_unique<VmParam>();

				vmParam->paramNameBase = nameBase;
				vmParam->paramNameVm = nameVm;
				vmParam->compBase = compBase;
				vmParam->compVm = compVm;

				vmParam->sliderListenerVm = new SliderListener(compVm, [this, vmParam = vmParam.get()](juce::Slider* _slider)
				{
					onVmSliderChanged(*vmParam);
				});

				m_vmParams.push_back(std::move(vmParam));
			}
		}

		m_btVmMap->onClick = [this]
		{
			toggleVmMap(m_btVmMap->getToggleState());
		};
		m_onCurrentPartChanged.set(c.onCurrentPartChanged, [this](const uint8_t&/* _part*/)
		{
			onCurrentPartChanged();
		});

		bindAll();
	}

	void VmMap::setEnabled(bool _enabled)
	{
		toggleVmMap(_enabled);
	}

	VmMap::VmParam::~VmParam()
	{
		delete sliderListenerVm;
	}

	void VmMap::toggleVmMap(const bool _enabled)
	{
		if(m_enabled == _enabled)
			return;

		m_enabled = _enabled;

		for (auto& vmParam : m_vmParams)
		{
			vmParam->compVm->setInterceptsMouseClicks(_enabled, _enabled);
			vmParam->compBase->setInterceptsMouseClicks(!_enabled, !_enabled);
			vmParam->compVm->setEnabled(_enabled);
			vmParam->compBase->setEnabled(!_enabled);
		}

		m_btVmMap->setToggleState(_enabled, juce::dontSendNotification);
	}

	void VmMap::onCurrentPartChanged() const
	{
		bindAll();
	}

	void VmMap::bindAll() const
	{
		const auto part = m_editor.getN2xController().getCurrentPart();

		for (auto& vmParamPtr : m_vmParams)
		{
			auto& vmParam = *vmParamPtr;

			auto* paramBase = m_editor.getN2xController().getParameter(vmParam.paramNameBase, part);
			auto* paramVm = m_editor.getN2xController().getParameter(vmParam.paramNameVm, part);

			vmParam.paramBase = paramBase;
			vmParam.paramVm = paramVm;

			vmParam.parameterListenerBase.set(paramBase, [&vmParam, this](pluginLib::Parameter* _parameter)
			{
				onBaseParamChanged(vmParam, _parameter);
			});

			vmParam.parameterListenerVm.set(paramVm, [&vmParam, this](pluginLib::Parameter* _parameter)
			{
				onVmParamChanged(vmParam, _parameter);
			});

			updateVmSlider(vmParam);
		}
	}

	void VmMap::onVmSliderChanged(const VmParam& _param) const
	{
		if (!_param.paramBase || !_param.paramVm)
			return;

		int value = juce::roundToInt(_param.compVm->getValue());
		value -= _param.paramBase->getUnnormalizedValue();

		_param.paramVm->setUnnormalizedValue(value, pluginLib::Parameter::Origin::Ui);

		m_editor.getFocusedParameter().updateByComponent(_param.compVm);
	}

	void VmMap::onBaseParamChanged(const VmParam& _vmParam, pluginLib::Parameter*)
	{
		updateVmSlider(_vmParam);
	}

	void VmMap::onVmParamChanged(const VmParam& _vmParam, pluginLib::Parameter*)
	{
		updateVmSlider(_vmParam);
	}

	void VmMap::updateVmSlider(const VmParam& _vmParam)
	{
		const auto& range = _vmParam.compBase->getRange();
		_vmParam.compVm->setRange(range.getStart(), range.getEnd());

		const auto baseValue = _vmParam.paramBase->getUnnormalizedValue();

		const auto offset = _vmParam.paramVm->getUnnormalizedValue();

		_vmParam.compVm->setValue(baseValue + offset, juce::dontSendNotification);
		_vmParam.compVm->setDoubleClickReturnValue(true, baseValue);

		_vmParam.compVm->setAlpha(offset != 0 ? 1.0f : 0.0f);
	}
}

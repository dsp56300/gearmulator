#include "n2xVmMap.h"

#include "n2xController.h"
#include "n2xEditor.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemValue.h"

namespace n2xJucePlugin
{
	constexpr const char* g_postfix = "Sens";

	class SliderListener : public Rml::EventListener
	{
	public:
		SliderListener() = delete;

		SliderListener(const SliderListener&) = delete;
		SliderListener(SliderListener&&) = default;

		SliderListener& operator=(const SliderListener&) = delete;
		SliderListener& operator=(SliderListener&&) = default;

		SliderListener(Rml::Element* _slider, const std::function<void(Rml::Element*)>& _onValueChanged) : m_slider(_slider), m_onValueChanged(_onValueChanged)
		{
			_slider->AddEventListener(Rml::EventId::Change, this);
		}

		~SliderListener() override
		{
			m_slider->RemoveEventListener(Rml::EventId::Change, this);
		}

		void ProcessEvent(Rml::Event& event) override
		{
			if (event.GetId() == Rml::EventId::Change)
			{
				if (m_onValueChanged)
					m_onValueChanged(m_slider);
			}
		}

	private:
		Rml::Element* m_slider;
		std::function<void(Rml::Element*)> m_onValueChanged;
	};

	VmMap::VmMap(Editor& _editor)
	: m_editor(_editor)
	, m_btVmMap(_editor.findChild("VMMAP"))
	{
		auto& c = _editor.getN2xController();

		const auto& descs = c.getParameterDescriptions().getDescriptions();

		auto* binding = _editor.getRmlParameterBinding();

		for (const auto& desc : descs)
		{
			uint32_t idxBase, idxVm;

			const auto& nameBase = desc.name;
			const auto& nameVm = desc.name + g_postfix;

			if(c.getParameterDescriptions().getIndexByName(idxBase, nameBase) &&
				c.getParameterDescriptions().getIndexByName(idxVm, nameVm))
			{
				auto* paramBase = c.getParameter(idxBase, c.getCurrentPart());
				auto* paramVm = c.getParameter(idxVm, c.getCurrentPart());

				auto* compBase = binding->getElementForParameter(paramBase, false);
				auto* compVm = binding->getElementForParameter(paramVm, false);

				if (!compVm || !compBase)
					continue;

				// on knobs, the shift key adjust the speed scale, we do not want this on vm sliders
				compVm->SetAttribute("speedScaleShift", 1.0f);

				// we do not want vm params to be bound at all, we do this manually. Remove any binding that might still be present in a skin
				binding->unbind(*compVm);

				auto vmParam = std::make_unique<VmParam>();

				vmParam->paramNameBase = nameBase;
				vmParam->paramNameVm = nameVm;
				vmParam->compBase = compBase;
				vmParam->compVm = compVm;

				vmParam->sliderListenerVm = new SliderListener(compVm, [this, vmParam = vmParam.get()](Rml::Element* _slider)
				{
					onVmSliderChanged(*vmParam);
				});

				m_vmParams.push_back(std::move(vmParam));
			}
		}

		juceRmlUi::EventListener::AddClick(m_btVmMap, [this]()
		{
			toggleVmMap(juceRmlUi::ElemButton::isChecked(m_btVmMap));
		});

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
			vmParam->compVm->SetProperty(Rml::PropertyId::PointerEvents, _enabled ? Rml::Style::PointerEvents::Auto : Rml::Style::PointerEvents::None);
			vmParam->compBase->SetProperty(Rml::PropertyId::PointerEvents, _enabled ? Rml::Style::PointerEvents::None : Rml::Style::PointerEvents::Auto);
		}

		juceRmlUi::ElemButton::setChecked(m_btVmMap, _enabled);
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

		int value = juce::roundToInt(juceRmlUi::ElemValue::getValue(_param.compVm));
		value -= _param.paramBase->getUnnormalizedValue();

		_param.paramVm->setUnnormalizedValue(value, pluginLib::Parameter::Origin::Ui);

		m_editor.getFocusedParameter().updateByElement(_param.compVm);
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
		const auto valMin = _vmParam.compBase->GetAttribute("min", 0.0f);
		const auto valMax = _vmParam.compBase->GetAttribute("max", 1.0f);

		_vmParam.compVm->SetAttribute("min", valMin);
		_vmParam.compVm->SetAttribute("max", valMax);

		const auto baseValue = _vmParam.paramBase->getUnnormalizedValue();

		const auto offset = _vmParam.paramVm->getUnnormalizedValue();

		juceRmlUi::ElemValue::setValue(_vmParam.compVm, static_cast<float>(baseValue + offset), false);

		_vmParam.compVm->SetAttribute("default", baseValue);

		_vmParam.compVm->SetProperty(Rml::PropertyId::Opacity, Rml::Property(offset != 0 ? 1.0f : 0.0f, Rml::Unit::NUMBER));
	}
}

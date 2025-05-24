#include "rmlParameterRef.h"

#include <cassert>

#include "rmlParameterBinding.h"
#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/DataModelHandle.h"
#include "RmlUi/Core/Variant.h"

namespace rmlPlugin
{
	RmlParameterRef::RmlParameterRef(RmlParameterBinding& _binding, pluginLib::Parameter* _param, uint32_t _paramIdx, Rml::DataModelConstructor& _model)
	: m_binding(_binding)
	, m_parameter(_param)
	, m_paramIdx(_paramIdx)
	, m_prefix(createVariableName(m_parameter->getDescription().name))
	, m_handle(_model.GetModelHandle())
	{
		assert(m_parameter != nullptr);

		m_listener.set(m_parameter, [this](pluginLib::Parameter* _p)
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(m_binding.getRmlComponent());
			onParameterValueChanged();
		});

		// unnormalized value
		_model.BindFunc(m_prefix + "_value", [this](Rml::Variant& _dest)
		{
			_dest = m_parameter->getUnnormalizedValue();
		}, [this](const Rml::Variant& _source)
		{
			const auto v = _source.Get<int>();

			juce::MessageManager::callAsync([this, v]()
			{
				m_parameter->setUnnormalizedValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);
			});
		});

		// parameter value formatted as text
		_model.BindFunc(m_prefix + "_text", [this](Rml::Variant& _dest)
		{
			_dest = m_parameter->getCurrentValueAsText().toStdString();
		}, [this](const Rml::Variant& _source)
		{
			auto text = _source.Get<Rml::String>();
			juce::MessageManager::callAsync([this, text]()
			{
				const auto v = m_parameter->getValueForText(text);
				m_parameter->setValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);
			});
		});

		// minimum value
		_model.BindFunc(m_prefix + "_min", [this](Rml::Variant& _dest)
		{
			_dest = m_parameter->getDescription().range.getStart();
		}, [this](const Rml::Variant& _source)
		{
			assert(false && "RmlParameterBinding::createParameterFunc: min value is not settable");
		});

		// maximum value
		_model.BindFunc(m_prefix + "_max", [this](Rml::Variant& _dest)
		{
			_dest = m_parameter->getDescription().range.getEnd();
		}, [this](const Rml::Variant& _source)
		{
			assert(false && "RmlParameterBinding::createParameterFunc: max value is not settable");
		});

		setParameter(_param);

		_model.GetModelHandle().DirtyVariable(m_prefix + "_value");
	}

	std::string RmlParameterRef::createVariableName(const std::string& _name)
	{
		std::string name = Rml::StringUtilities::ToLower(_name);

		// replace all non-alphanumeric characters with underscores
		for (auto& c : name)
		{
			if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
				c = '_';
		}
		return name;
	}

	void RmlParameterRef::setDirty()
	{
		m_handle.DirtyVariable(m_prefix + "_value");
		m_handle.DirtyVariable(m_prefix + "_text");
	}

	void RmlParameterRef::onParameterValueChanged()
	{
		setDirty();
	}

	void RmlParameterRef::setParameter(pluginLib::Parameter* _param)
	{
		assert(_param);

		const auto dirty = !m_parameter || m_parameter->getUnnormalizedValue() != _param->getUnnormalizedValue();

		m_parameter = _param;

		if (dirty)
			setDirty();

		m_listener.set(m_parameter);
	}

	void RmlParameterRef::changePart(const pluginLib::Controller& _controller, const uint8_t _part)
	{
		setParameter(_controller.getParameter(m_paramIdx, _part));
	}
}

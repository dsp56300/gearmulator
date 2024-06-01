
#include "parameterlink.h"

#include "parameter.h"

namespace pluginLib
{
	ParameterLink::ParameterLink(Parameter* _source, Parameter* _dest) : m_source(_source), m_sourceListener(_source)
	{
		m_sourceValue = _source->getUnnormalizedValue();

		m_sourceListener = [this](Parameter*)
		{
			onSourceValueChanged();
		};

		_source->setLinkState(Source);
		add(_dest);
	}

	ParameterLink::~ParameterLink()
	{
		m_source->clearLinkState(Source);
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	bool ParameterLink::add(Parameter* _target)
	{
		if(!_target)
			return false;

		if(!m_targets.insert(_target).second)
			return false;

		_target->setUnnormalizedValue(m_sourceValue, Parameter::Origin::Ui);

		return true;
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	bool ParameterLink::remove(Parameter* _target)
	{
		const auto it = m_targets.find(_target);
		if(it == m_targets.end())
			return false;
		m_targets.erase(it);
		return true;
	}

	void ParameterLink::onSourceValueChanged()
	{
		const auto newValue = m_source->getUnnormalizedValue();

		if(newValue == m_sourceValue)
			return;

		m_sourceValue = newValue;

		const auto origin = m_source->getChangeOrigin();

		for (auto* p : m_targets)
		{
			const auto v = p->getDescription().range.clipValue(newValue);
			p->setUnnormalizedValue(v, origin);
		}
	}
}

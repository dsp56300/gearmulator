
#include "parameterlink.h"

#include "parameter.h"

namespace pluginLib
{
	ParameterLink::ParameterLink(Parameter* _source, Parameter* _dest, bool _applyCurrentSourceToTarget) : m_source(_source), m_sourceListener(_source)
	{
		m_sourceValue = _source->getUnnormalizedValue();

		m_sourceListener = [this](Parameter*)
		{
			onSourceValueChanged();
		};

		_source->setLinkState(Source);
		add(_dest, _applyCurrentSourceToTarget);
	}

	ParameterLink::~ParameterLink()
	{
		m_source->clearLinkState(Source);
	}

	bool ParameterLink::add(Parameter* _target, bool _applyCurrentSourceToTarget)
	{
		if(!_target)
			return false;

		if(!m_targets.insert(_target).second)
			return false;

		if(_applyCurrentSourceToTarget)
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
		const auto newSourceValue = m_source->getUnnormalizedValue();

		if(newSourceValue == m_sourceValue)
			return;

		const auto sourceDiff = newSourceValue - m_sourceValue;

		m_sourceValue = newSourceValue;

		// do not apply to linked parameters if the change is caused by a preset load
		if(m_source->getChangeOrigin() == Parameter::Origin::PresetChange)
			return;

		const auto origin = m_source->getChangeOrigin();

		for (auto* p : m_targets)
		{
			const auto newTargetValue = p->getUnnormalizedValue() + sourceDiff;
			const auto clampedTargetValue = p->getDescription().range.clipValue(newTargetValue);
			p->setUnnormalizedValue(clampedTargetValue, origin);
		}
	}
}

#pragma once

#include <set>

#include "parameterlistener.h"

namespace pluginLib
{
	class Parameter;

	class ParameterLink
	{
	public:
		ParameterLink(Parameter* _source, Parameter* _dest, bool _applyCurrentSourceToTarget);
		~ParameterLink();

		bool add(Parameter* _target, bool _applyCurrentSourceToTarget);
		bool remove(Parameter* _target);

		bool empty() const { return m_targets.empty(); }
		bool hasTarget(const Parameter* _target) const { return m_targets.count(const_cast<Parameter*>(_target)) > 0; }

	private:
		void onSourceValueChanged();

		Parameter* const m_source;
		ParameterListener m_sourceListener;
		std::set<Parameter*> m_targets;
		int m_sourceValue;
		bool m_propagating = false;
	};
}

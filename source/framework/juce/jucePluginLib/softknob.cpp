#include "softknob.h"

#include <cassert>

#include "controller.h"

namespace pluginLib
{
	SoftKnob::SoftKnob(const Controller& _controller, const uint8_t _part, const uint32_t _parameterIndex)
		: m_controller(_controller)
		, m_part(_part)
	{
		m_sourceParam = _controller.getParameter(_parameterIndex, _part);
		assert(m_sourceParam);

		const auto& desc = m_sourceParam->getDescription();

		m_targetSelect = _controller.getParameter(desc.softKnobTargetSelect, _part);
		assert(m_targetSelect);

		m_targetSelectListener.set(m_targetSelect, [this](auto*)
		{
			onTargetChanged();
		});

		m_sourceParamListener.set(m_sourceParam, [this](auto*)
		{
			onSourceValueChanged();
		});

		bind();
	}

	SoftKnob::~SoftKnob()
	{
		unbind();
		m_sourceParamListener.reset();
		m_targetSelectListener.reset();
	}

	void SoftKnob::update()
	{
		if(findTarget() != m_targetParam)
			bind();
		else if(m_targetParam)
			onTargetValueChanged();
	}

	void SoftKnob::onTargetChanged()
	{
		update();
	}

	void SoftKnob::onSourceValueChanged() const
	{
		if(!m_targetParam)
			return;

		const auto v = m_sourceParam->getUnnormalizedValue();
		m_targetParam->setUnnormalizedValue(v, Parameter::Origin::Derived);
	}

	void SoftKnob::onTargetValueChanged() const
	{
		assert(m_targetParam);
		const auto v = m_targetParam->getUnnormalizedValue();
		m_sourceParam->setUnnormalizedValue(v, Parameter::Origin::Derived);
	}

	Parameter* SoftKnob::findTarget() const
	{
		const auto* valueList = m_controller.getParameterDescriptions().getValueList(m_sourceParam->getDescription().softKnobTargetList);
		if(!valueList)
			return nullptr;

		const auto& targets = valueList->texts;

		const auto targetIndex = m_targetSelect->getUnnormalizedValue();

		if(targetIndex < 0 || targetIndex >= static_cast<int>(targets.size()))
			return nullptr;

		const auto& targetName = targets[targetIndex];
		if(targetName.empty())
			return nullptr;

		return m_controller.getParameter(targetName, m_part);
	}

	void SoftKnob::bind()
	{
		unbind();

		m_targetParam = findTarget();
		if(!m_targetParam)
			return;

		m_targetParamListener.set(m_targetParam, [this](Parameter*)
		{
			onTargetValueChanged();
		});

		onBind(m_targetParam);

		onTargetValueChanged();
	}

	void SoftKnob::unbind()
	{
		m_targetParamListener.reset();
		m_targetParam = nullptr;
		onBind(nullptr);
	}
}

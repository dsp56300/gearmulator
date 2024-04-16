#include "softknob.h"

#include <cassert>

#include "controller.h"

namespace pluginLib
{
	namespace
	{
		uint32_t g_softKnobListenerId = 0x50f750f7;
	}

	SoftKnob::SoftKnob(const Controller& _controller, const uint8_t _part, const uint32_t _parameterIndex)
		: m_controller(_controller)
		, m_part(_part)
		, m_uniqueId(g_softKnobListenerId++)
	{
		m_param = _controller.getParameter(_parameterIndex, _part);
		assert(m_param);

		const auto& desc = m_param->getDescription();

		const auto idxTargetSelect = _controller.getParameterIndexByName(desc.softKnobTargetSelect);
		assert(idxTargetSelect != Controller::InvalidParameterIndex);

		m_targetSelect = _controller.getParameter(idxTargetSelect, _part);
		assert(m_targetSelect);

		m_targetSelect->onValueChanged.emplace_back(m_uniqueId, [this]()
		{
			onTargetChanged();
		});

		m_param->onValueChanged.emplace_back(m_uniqueId, [this]()
		{
			onSourceValueChanged();
		});

		bind();
	}

	SoftKnob::~SoftKnob()
	{
		unbind();
		m_param->removeListener(m_uniqueId);
		m_targetSelect->removeListener(m_uniqueId);
	}

	void SoftKnob::onTargetChanged()
	{
		bind();
	}

	void SoftKnob::onSourceValueChanged()
	{
		if(!m_targetParam)
			return;

		const auto v = m_param->getValue();
		m_targetParam->setValue(v, Parameter::ChangedBy::Derived);
	}

	void SoftKnob::onTargetValueChanged()
	{
		assert(m_targetParam);
		const auto v = m_targetParam->getValue();
		m_param->setValue(v, Parameter::ChangedBy::Derived);
	}

	void SoftKnob::bind()
	{
		unbind();

		const auto* valueList = m_controller.getParameterDescriptions().getValueList(m_param->getDescription().softKnobTargetList);
		if(!valueList)
			return;

		const auto& targets = valueList->texts;

		const auto targetIndex = m_targetSelect->getUnnormalizedValue();

		if(targetIndex < 0 || targetIndex >= static_cast<int>(targets.size()))
			return;

		const auto targetName = targets[targetIndex];
		if(targetName.empty())
			return;

		const auto targetParamIdx = m_controller.getParameterIndexByName(targetName);
		if(targetParamIdx == Controller::InvalidParameterIndex)
			return;

		m_targetParam = m_controller.getParameter(targetParamIdx, m_part);
		if(!m_targetParam)
			return;

		m_targetParam->onValueChanged.emplace_back(m_uniqueId, [this]()
		{
			onTargetValueChanged();
		});

		onTargetValueChanged();
	}

	void SoftKnob::unbind()
	{
		if(m_targetParam)
			m_targetParam->removeListener(m_uniqueId);
		m_targetParam = nullptr;
	}
}

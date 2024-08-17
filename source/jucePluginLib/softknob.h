#pragma once

#include <cstdint>

#include "parameterlistener.h"

namespace pluginLib
{
	class Controller;

	class SoftKnob
	{
	public:
		Event<Parameter*> onBind;

		SoftKnob(const Controller& _controller, uint8_t _part, uint32_t _parameterIndex);
		~SoftKnob();

		SoftKnob(const SoftKnob&) = delete;
		SoftKnob(SoftKnob&&) = delete;
		SoftKnob& operator = (const SoftKnob&) = delete;
		SoftKnob& operator = (SoftKnob&&) = delete;

		bool isValid() const { return m_sourceParam != nullptr && m_targetSelect != nullptr; }
		bool isBound() const { return m_targetParam != nullptr; }

		Parameter* getParameter() const { return m_sourceParam; }
		Parameter* getTargetParameter() const { return m_targetParam; }

	private:
		void onTargetChanged();
		void onSourceValueChanged() const;
		void onTargetValueChanged() const;

		void bind();
		void unbind();

		const Controller& m_controller;
		const uint8_t m_part;

		Parameter* m_sourceParam = nullptr;
		Parameter* m_targetSelect = nullptr;
		Parameter* m_targetParam = nullptr;

		ParameterListener m_sourceParamListener;
		ParameterListener m_targetSelectListener;
		ParameterListener m_targetParamListener;
	};
}

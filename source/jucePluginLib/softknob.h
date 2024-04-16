#pragma once

#include <cstdint>

namespace pluginLib
{
	class Parameter;
	class Controller;

	class SoftKnob
	{
	public:
		SoftKnob(const Controller& _controller, uint8_t _part, uint32_t _parameterIndex);
		~SoftKnob();

		SoftKnob(const SoftKnob&) = delete;
		SoftKnob(SoftKnob&&) = delete;
		SoftKnob& operator = (const SoftKnob&) = delete;
		SoftKnob& operator = (SoftKnob&&) = delete;

		bool isValid() const { return m_param != nullptr && m_targetSelect != nullptr; }
		bool isBound() const { return m_targetParam != nullptr; }

		Parameter* getParameter() const { return m_param; }
		Parameter* getTargetParameter() const { return m_targetParam; }

	private:
		void onTargetChanged();
		void onSourceValueChanged();
		void onTargetValueChanged();

		void bind();
		void unbind();

		const Controller& m_controller;
		const uint8_t m_part;
		const uint32_t m_uniqueId;

		Parameter* m_param = nullptr;
		Parameter* m_targetSelect = nullptr;
		Parameter* m_targetParam = nullptr;
	};
}

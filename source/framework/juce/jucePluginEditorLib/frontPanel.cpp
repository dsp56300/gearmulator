#include "frontPanel.h"

#include <algorithm>
#include <cmath>

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "jucePluginLib/controller.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlEventListener.h"

namespace jucePluginEditorLib
{
	namespace
	{
		constexpr float g_encoderMin = 0.0f;
		constexpr float g_encoderMax = 10.0f;
	}

	FrontPanel::FrontPanel(const Editor& _editor, Binding _binding)
		: m_controller(_editor.getProcessor().getController())
		, m_binding(std::move(_binding))
	{
		for (const auto& id : m_binding.leds)
			m_leds.push_back(_editor.findChild(id, false));

		for (size_t i=0; i<m_binding.buttons.size(); ++i)
		{
			auto* b = _editor.findChild<juceRmlUi::ElemButton>(m_binding.buttons[i], false);
			if (!b)
				continue;

			const auto index = static_cast<uint32_t>(i);

			auto send = [this, index](const bool _pressed)
			{
				std::vector<synthLib::SMidiEvent> events;
				synthLib::fp::sendButton(events, index, _pressed);
				m_controller.sendFrontPanel(std::move(events));
			};

			if (b->isToggle())
			{
				juceRmlUi::EventListener::Add(b, Rml::EventId::Change, [b, send](Rml::Event&)
				{
					send(b->isChecked());
				});
			}
			else
			{
				juceRmlUi::EventListener::Add(b, Rml::EventId::Mousedown, [b, send](Rml::Event&)
				{
					b->setChecked(true);
					send(true);
				});
				juceRmlUi::EventListener::Add(b, Rml::EventId::Mouseup, [b, send](Rml::Event&)
				{
					b->setChecked(false);
					send(false);
				});
			}
		}

		for (size_t i=0; i<m_binding.encoders.size(); ++i)
		{
			auto* knob = dynamic_cast<juceRmlUi::ElemKnob*>(_editor.findChild(m_binding.encoders[i], false));
			m_encoders.push_back(knob);
			m_encoderValues.push_back(g_encoderMin + (g_encoderMax - g_encoderMin) * 0.5f);

			if (!knob)
				continue;

			knob->setMinValue(g_encoderMin);
			knob->setMaxValue(g_encoderMax);
			knob->setValue(m_encoderValues.back(), false);
			knob->setEndless(true);

			juceRmlUi::EventListener::Add(knob, Rml::EventId::Change, [this, i](Rml::Event&)
			{
				onEncoderChanged(i);
			});
		}

		// the shadow on top of the LCD must not take the clicks meant for it
		if (auto* shadow = _editor.findChild("lcdshadow", false))
			shadow->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);

		auto& transport = m_controller.getFrontPanel();

		// the events are retained, these get the current state right away
		m_onLcdText.set(transport.evLcdText, [this](const uint32_t _cols, const uint32_t _rows, const std::vector<uint8_t>& _cells)
		{
			if (m_binding.lcdSink && _cols == m_binding.lcdCols && _rows == m_binding.lcdRows)
				m_binding.lcdSink(_cells);
		});

		m_onLcdCgRam.set(transport.evLcdCgRam, [this](const std::array<uint8_t, synthLib::fp::g_cgRamSize>& _cgRam)
		{
			if (m_binding.cgRamSink)
				m_binding.cgRamSink(_cgRam);
		});

		m_onLedMask.set(transport.evLedMask, [this](const std::vector<bool>& _leds)
		{
			for (size_t i=0; i<std::min(_leds.size(), m_leds.size()); ++i)
			{
				if (m_leds[i])
					juceRmlUi::ElemButton::setChecked(m_leds[i], _leds[i]);
			}
		});

		// the device sends changes only, a panel created later has to ask for the rest
		std::vector<synthLib::SMidiEvent> request;
		synthLib::fp::sendRequestState(request);
		m_controller.sendFrontPanel(std::move(request));
	}

	FrontPanel::~FrontPanel() = default;

	void FrontPanel::onEncoderChanged(const size_t _index)
	{
		const auto v = juceRmlUi::ElemValue::getValue(m_encoders[_index]);
		auto& vOld = m_encoderValues[_index];

		// the knob is endless and wraps from max to min and back
		constexpr auto range = g_encoderMax - g_encoderMin;
		auto delta = v - vOld;
		if (delta > range * 0.5f)
			delta -= range;
		else if (delta < -range * 0.5f)
			delta += range;

		const auto steps = static_cast<int32_t>(std::lround(m_binding.encoderSpeed * delta));
		if (steps == 0)
			return;

		vOld = v;

		std::vector<synthLib::SMidiEvent> events;
		synthLib::fp::sendEncoder(events, static_cast<uint32_t>(_index), steps);
		m_controller.sendFrontPanel(std::move(events));
	}
}

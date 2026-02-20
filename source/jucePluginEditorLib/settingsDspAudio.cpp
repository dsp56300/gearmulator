#include "settingsDspAudio.h"

#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "juceUiLib/messageBox.h"

#include "RmlUi/Core/Element.h"

#include <cmath>
#include <sstream>

namespace jucePluginEditorLib
{
	static constexpr std::initializer_list<uint32_t> g_latencies = {0, 1, 2, 4, 8};
	static constexpr std::initializer_list<int> g_clockPercents = {50, 75, 100, 125, 150, 200};

	struct ResamplerModeEntry
	{
		synthLib::Resampler::Mode mode;
		const char* buttonId;
	};

	static constexpr std::initializer_list<ResamplerModeEntry> g_resamplerModes =
	{
		{synthLib::Resampler::Mode::Legacy, "btResamplerLegacy"},
		{synthLib::Resampler::Mode::MameHq, "btResamplerHq"},
		{synthLib::Resampler::Mode::MameLofi, "btResamplerLofi"}
	};

	static_assert(g_resamplerModes.size() == static_cast<size_t>(synthLib::Resampler::Mode::Count));

	namespace
	{
		float dbToGain(const float _db)
		{
			return std::pow(10.0f, _db / 20.0f);
		}

		float gainToDb(const float _gain)
		{
			return 20.0f * std::log10(_gain);
		}

		std::string formatDbLabel(float _db)
		{
			const auto dbInt = static_cast<int>(std::round(_db));
			if (dbInt > 0)
				return "+" + std::to_string(dbInt) + " dB";
			if (dbInt < 0)
				return std::to_string(dbInt) + " dB";
			return "0 dB";
		}
	}

	void SettingsDspAudio::createUi(Rml::Element* _root)
	{
		// Latency buttons
		const auto currentLatency = getCurrentLatency();

		for (const auto latency : g_latencies)
		{
			const auto buttonId = "btLatency" + std::to_string(latency);
			auto* buttonContainer = juceRmlUi::helper::findChild(_root, buttonId);
			if (!buttonContainer)
				continue;

			auto* button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(buttonContainer, "button");
			if (!button)
				continue;

			m_latencyButtons.emplace_back(latency, button);

			button->setChecked(currentLatency == latency);

			juceRmlUi::EventListener::Add(buttonContainer, Rml::EventId::Click, [this, latency](Rml::Event& _event)
			{
				_event.StopPropagation();

				m_processor.setLatencyBlocks(latency);

				updateButtons();

				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Warning",
					"Most hosts cannot handle if a plugin changes its latency while being in use.\n"
					"It is advised to save, close & reopen the project to prevent synchronization issues.");
			});
		}

		// DSP Clock buttons
		if (m_processor.canModifyDspClock())
		{
			if (auto* dspClockTemplate = juceRmlUi::helper::findChild(_root, "dspClockEntry", false))
			{
				const auto currentPercent = m_processor.getDspClockPercent();
				const auto hz = m_processor.getDspClockHz();
				auto* parent = dspClockTemplate->GetParentNode();

				for (const auto percent : g_clockPercents)
				{
					auto clone = dspClockTemplate->Clone();
					auto* clonePtr = clone.get();
					clonePtr->SetId("");
					clonePtr->RemoveProperty("display");

					auto* buttonContainer = juceRmlUi::helper::findChild(clonePtr, "dspClockButton");
					auto* button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(buttonContainer, "button");
					auto* label = juceRmlUi::helper::findChild(buttonContainer, "label");

					const auto mhz = hz * percent / 100 / 1000000;
					std::stringstream ss;
					ss << percent << "% (" << mhz << " MHz)";
					if(percent == 100)
						ss << " (Default)";
					
					label->SetInnerRML(ss.str());

					button->setChecked(currentPercent == percent);
					m_clockButtons.emplace_back(percent, button);

					juceRmlUi::EventListener::Add(buttonContainer, Rml::EventId::Click, [this, percent](Rml::Event& _event)
					{
						_event.StopPropagation();
						m_processor.setDspClockPercent(percent);
						updateClockButtons();
					});

					parent->InsertBefore(std::move(clone), dspClockTemplate);
				}

				dspClockTemplate->GetParentNode()->RemoveChild(dspClockTemplate);
			}
		}
		else
		{
			if (auto* containerDspClock = juceRmlUi::helper::findChild(_root, "containerDspClock", false))
			{
				juceRmlUi::helper::setVisible(containerDspClock, false);
			}
		}

		// Output Gain slider
		auto* sliderGain = juceRmlUi::helper::findChild(_root, "sliderGain", false);
		auto* labelGain = juceRmlUi::helper::findChild(_root, "labelGain", false);

		if (sliderGain && labelGain)
		{
			// Initialize slider with current gain value (convert to dB)
			const float currentGain = m_processor.getOutputGain();
			const float currentDb = std::round(gainToDb(currentGain));
			sliderGain->SetAttribute("value", std::to_string(currentDb));
			labelGain->SetInnerRML(Rml::StringUtilities::EncodeRml(formatDbLabel(currentDb)));

			// Add change event listener
			juceRmlUi::EventListener::Add(sliderGain, Rml::EventId::Change, [this, sliderGain, labelGain](Rml::Event& _event)
			{
				_event.StopPropagation();

				// Get dB value from slider
				const auto* valueAttr = sliderGain->GetAttribute("value");
				if (!valueAttr)
					return;

				const float db = valueAttr->Get<float>(sliderGain->GetCoreInstance());

				// Convert to gain and set
				const float gain = dbToGain(db);
				m_processor.setOutputGain(gain);

				// Update label
				labelGain->SetInnerRML(Rml::StringUtilities::EncodeRml(formatDbLabel(db)));
			});
		}

		// Resampler mode buttons
		const auto currentResamplerMode = m_processor.getResamplerMode();

		for (const auto& [mode, buttonId] : g_resamplerModes)
		{
			auto* buttonContainer = juceRmlUi::helper::findChild(_root, buttonId, false);
			if (!buttonContainer)
				continue;

			auto* button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(buttonContainer, "button");
			if (!button)
				continue;

			m_resamplerButtons.emplace_back(mode, button);

			button->setChecked(currentResamplerMode == mode);

			juceRmlUi::EventListener::Add(buttonContainer, Rml::EventId::Click, [this, mode](Rml::Event& _event)
			{
				_event.StopPropagation();
				m_processor.setResamplerMode(mode);
				updateResamplerButtons();
			});
		}
	}

	uint32_t SettingsDspAudio::getCurrentLatency() const
	{
		return m_processor.getPlugin().getLatencyBlocks();
	}

	void SettingsDspAudio::updateButtons() const
	{
		const auto currentLatency = getCurrentLatency();

		for (const auto [latency, button] : m_latencyButtons)
		{
			button->setChecked(currentLatency == latency);
		}
	}

	void SettingsDspAudio::updateClockButtons() const
	{
		const auto currentPercent = m_processor.getDspClockPercent();

		for (const auto [percent, button] : m_clockButtons)
		{
			button->setChecked(currentPercent == percent);
		}
	}

	void SettingsDspAudio::updateResamplerButtons() const
	{
		const auto currentMode = m_processor.getResamplerMode();

		for (const auto [mode, button] : m_resamplerButtons)
		{
			button->setChecked(currentMode == mode);
		}
	}
}

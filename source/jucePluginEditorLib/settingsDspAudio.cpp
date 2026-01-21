#include "settingsDspAudio.h"

#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "juceUiLib/messageBox.h"

#include <cmath>

namespace jucePluginEditorLib
{
	static constexpr std::initializer_list<uint32_t> g_latencies = {0, 1, 2, 4, 8};

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

			juceRmlUi::EventListener::Add(button, Rml::EventId::Click, [this, latency](Rml::Event& _event)
			{
				_event.StopPropagation();

				m_processor.setLatencyBlocks(latency);

				updateButtons();

				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Warning",
					"Most hosts cannot handle if a plugin changes its latency while being in use.\n"
					"It is advised to save, close & reopen the project to prevent synchronization issues.");
			});
		}

		// Output Gain slider
		auto* sliderGain = juceRmlUi::helper::findChild(_root, "sliderGain", false);
		auto* labelGain = juceRmlUi::helper::findChild(_root, "labelGain", false);

		if (sliderGain && labelGain)
		{
			// Initialize slider with current gain value (convert to dB)
			const float currentGain = m_processor.getOutputGain();
			const float currentDb = gainToDb(currentGain);
			sliderGain->SetAttribute("value", std::to_string(std::round(currentDb)));
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
}

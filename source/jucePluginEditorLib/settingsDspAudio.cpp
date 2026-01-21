#include "settingsDspAudio.h"

#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib
{
	static constexpr std::initializer_list<uint32_t> g_latencies = {0, 1, 2, 4, 8};

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

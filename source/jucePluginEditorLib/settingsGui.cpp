#include "settingsGui.h"

#include "pluginProcessor.h"
#include "pluginEditorState.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

namespace jucePluginEditorLib
{
	static constexpr std::initializer_list<int> g_scales = {50, 65, 75, 85, 100, 125, 150, 175, 200, 250, 300, 400};

	void SettingsGui::createUi(Rml::Element* _root)
	{
		createToggleButton(_root, "btForceSoftwareRendering", "forceSoftwareRenderer", [this](bool)
		{
			juce::MessageManager::callAsync([this]
			{
				auto* editorState = m_processor.getEditorState();
				editorState->loadSkin(editorState->getCurrentSkin());
			});
		});

		// GUI Scale buttons
		const auto currentScale = getCurrentScale();

		for (const auto scale : g_scales)
		{
			const auto buttonId = "btScale" + std::to_string(scale);
			auto* buttonContainer = juceRmlUi::helper::findChild(_root, buttonId);
			if (!buttonContainer)
				continue;

			auto* button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(buttonContainer, "button");
			if (!button)
				continue;

			m_scaleButtons.emplace_back(scale, button);

			button->setChecked(currentScale == scale);

			juceRmlUi::EventListener::Add(buttonContainer, Rml::EventId::Click, [this, scale](Rml::Event& _event)
			{
				_event.StopPropagation();

				m_processor.getConfig().setValue("scale", juce::var(scale));

				updateButtons();

				const auto* editorState = m_processor.getEditorState();
				editorState->evSetGuiScale(scale);
			});
		}
	}

	int SettingsGui::getCurrentScale() const
	{
		auto& config = m_processor.getConfig();
		const auto currentScale = juce::roundToInt(config.getDoubleValue("scale", 100));
		return currentScale;
	}

	void SettingsGui::updateButtons() const
	{
		const int currentScale = getCurrentScale();

		for (const auto [scale, button] : m_scaleButtons)
		{
			button->setChecked(currentScale == scale);
		}
	}
}

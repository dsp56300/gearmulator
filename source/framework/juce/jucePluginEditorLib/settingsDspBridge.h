#pragma once

#include "settingsPlugin.h"

#include <juce_events/juce_events.h>

namespace bridgeClient
{
	struct ServerListEntry;
}

namespace juceRmlUi
{
	class EventListener;
	class ElemButton;
}

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Processor;

	class SettingsDspBridge : public SettingsPlugin, juce::Timer
	{
	public:
		explicit SettingsDspBridge(Processor& _processor);

		std::string getCategoryName() const override {return "DSP Bridge";}
		std::string getTemplateName() const override { return "tus_settings_dspbridge"; }

		void createUi(Rml::Element* _root) override;
		void timerCallback() override;

	private:
		void updateButtons() const;
		void refreshServerList();
		void initializeEntry(size_t _index, const std::string& _labelText, bool _isLocal, const std::string& _host = "", uint32_t _port = 0, bool _isError = false);

		struct DeviceEntry
		{
			Rml::Element* row = nullptr;
			juceRmlUi::ElemButton* button = nullptr;
			Rml::Element* label = nullptr;
			juceRmlUi::EventListener* clickListener = nullptr;
			std::string host;
			uint32_t port = 0;
			bool isLocal = false;
		};

		std::vector<DeviceEntry> m_deviceEntries;
		Rml::Element* m_table = nullptr;
		Rml::Element* m_templateRow = nullptr;
	};
}

#include "settingsDspBridge.h"

#include "pluginProcessor.h"
#include "pluginEditorState.h"

#include "client/serverList.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

namespace jucePluginEditorLib
{
	namespace
	{
		constexpr const char* g_deviceTypeTableId = "deviceTypeTable";
		constexpr const char* g_deviceTypeEntryId = "deviceTypeEntry";
		constexpr const char* g_deviceTypeButtonId = "deviceTypeButton";
		constexpr const char* g_buttonId = "button";
		constexpr const char* g_labelId = "label";

		std::tuple<juceRmlUi::ElemButton*, Rml::Element*> findDeviceButton(Rml::Element* _row)
		{
			auto* buttonContainer = juceRmlUi::helper::findChild(_row, g_deviceTypeButtonId);
			if (!buttonContainer)
				return {nullptr, nullptr};

			auto* button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(buttonContainer, g_buttonId);
			auto* label = juceRmlUi::helper::findChild(buttonContainer, g_labelId);

			return {button, label};
		}
	}

	SettingsDspBridge::SettingsDspBridge(Processor& _processor): SettingsPlugin(_processor)
	{
		startTimer(1000);
	}

	void SettingsDspBridge::createUi(Rml::Element* _root)
	{
		m_table = juceRmlUi::helper::findChild(_root, g_deviceTypeTableId);
		if (!m_table)
			return;

		m_templateRow = juceRmlUi::helper::findChild(_root, g_deviceTypeEntryId);
		if (!m_templateRow)
			return;

		createToggleButton(_root, "btEnableDspBridge", "supportDspBridge", [this](bool _enable)
		{
			m_processor.getEditorState()->enableDspBridge(_enable);
		});

		juceRmlUi::helper::setVisible(m_templateRow, false);

		// Initialize with current server list
		refreshServerList();
	}

	void SettingsDspBridge::timerCallback()
	{
		refreshServerList();
	}

	void SettingsDspBridge::initializeEntry(const size_t _index, const std::string& _labelText, const bool _isLocal, const std::string& _host, uint32_t _port, const bool _isError)
	{
		if (_index >= m_deviceEntries.size())
			return;

		auto& entry = m_deviceEntries[_index];

		// Update stored data
		entry.host = _host;
		entry.port = _port;
		entry.isLocal = _isLocal;

		// Show the row
		juceRmlUi::helper::setVisible(entry.row, true);

		if (!entry.button || !entry.label)
			return;

		// Update label
		entry.label->SetInnerRML(Rml::StringUtilities::EncodeRml(_labelText));

		if (_isError)
		{
			// Disable error entries
			entry.button->SetPseudoClass("disabled", true);
			entry.button->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			entry.button->setChecked(false);
		}
		else
		{
			// Enable entry
			entry.button->SetPseudoClass("disabled", false);
			entry.button->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::Auto);

			// Update checked state
			if (_isLocal)
			{
				const bool isSelected = m_processor.getDeviceType() == pluginLib::DeviceType::Local;
				entry.button->setChecked(isSelected);
			}
			else
			{
				const bool isSelected = m_processor.getDeviceType() == pluginLib::DeviceType::Remote &&
					m_processor.getRemoteDeviceHost() == _host &&
					m_processor.getRemoteDevicePort() == _port;
				entry.button->setChecked(isSelected);
			}

			if (entry.clickListener)
				entry.clickListener->Remove();

			// Add click handler
			if (_isLocal)
			{
				entry.clickListener = juceRmlUi::EventListener::Add(entry.button, Rml::EventId::Click, [this](Rml::Event& _event)
				{
					_event.StopPropagation();
					m_processor.setDeviceType(pluginLib::DeviceType::Local);
					updateButtons();
				});
			}
			else
			{
				entry.clickListener = juceRmlUi::EventListener::Add(entry.button, Rml::EventId::Click, [this, host = _host, port = _port](Rml::Event& _event)
				{
					_event.StopPropagation();
					m_processor.setRemoteDevice(host, port);
					updateButtons();
				});
			}
		}
	}

	void SettingsDspBridge::refreshServerList()
	{
		if (!m_table || !m_templateRow)
			return;

		std::set<bridgeClient::ServerList::Entry> servers;

		auto* editorState = m_processor.getEditorState();

		if (editorState->getRemoteServerList())
			servers = editorState->getRemoteServerList()->getEntries();

		// Calculate total entries needed: 1 (Local) + servers.size() or 1 (Local) + 1 ("no servers found")
		size_t entriesNeeded = 1; // Always have Local

		if (servers.empty())
			++entriesNeeded; // Add "no servers found" entry
		else
			entriesNeeded += servers.size();

		// Create more entries if needed
		while (m_deviceEntries.size() < entriesNeeded)
		{
			DeviceEntry newEntry;
			newEntry.row = m_table->AppendChild(m_templateRow->Clone());
			auto [button, label] = findDeviceButton(newEntry.row);
			newEntry.button = button;
			newEntry.label = label;
			m_deviceEntries.push_back(newEntry);
		}

		size_t currentIndex = 0;

		// Initialize Local device entry (always index 0)
		initializeEntry(currentIndex++, "Local (default)", true);

		// Initialize server entries
		if (servers.empty())
		{
			initializeEntry(currentIndex++, "- no servers found -", false, "", 0, true);
		}
		else
		{
			for (const auto& server : servers)
			{
				if (server.err.code == bridgeLib::ErrorCode::Ok)
				{
					const std::string name = server.host + ':' + std::to_string(server.serverInfo.portTcp);
					initializeEntry(currentIndex, name, false, server.host, server.serverInfo.portTcp, false);
				}
				else
				{
					const std::string name = server.host + " (error " + std::to_string(static_cast<uint32_t>(server.err.code)) + ", " + server.err.msg + ')';
					initializeEntry(currentIndex, name, false, server.host, server.serverInfo.portTcp, true);
				}
				currentIndex++;
			}
		}

		// Hide unused entries
		for (size_t i = currentIndex; i < m_deviceEntries.size(); ++i)
		{
			if (m_deviceEntries[i].row)
				juceRmlUi::helper::setVisible(m_deviceEntries[i].row, false);
		}
	}

	void SettingsDspBridge::updateButtons() const
	{
		for (const auto& entry : m_deviceEntries)
		{
			if (!entry.button || !juceRmlUi::helper::isVisible(entry.row))
				continue;

			if (entry.isLocal)
			{
				entry.button->setChecked(m_processor.getDeviceType() == pluginLib::DeviceType::Local);
			}
			else
			{
				const bool isSelected = m_processor.getDeviceType() == pluginLib::DeviceType::Remote &&
					m_processor.getRemoteDeviceHost() == entry.host &&
					m_processor.getRemoteDevicePort() == entry.port;
				entry.button->setChecked(isSelected);
			}
		}
	}
}

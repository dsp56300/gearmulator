#include "settingsPlugin.h"

#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

namespace jucePluginEditorLib
{
	bool SettingsPlugin::addClickHandler(Rml::Element* _root, const std::string& _child, std::function<void(Rml::Event&)> _handler)
	{
		auto* elem = juceRmlUi::helper::findChild(_root, _child, false);
		if (!elem)
			return false;
		juceRmlUi::EventListener::Add(elem, Rml::EventId::Click, [handler = std::move(_handler)](Rml::Event& _event)
		{
			_event.StopPropagation();
			handler(_event);
		});
		return true;
	}

	bool SettingsPlugin::createToggleButton(Rml::Element* _root, const std::string& _buttonName, juce::PropertiesFile& _config, const std::string& _configName, const std::function<void(bool)>& _changeCallback)
	{
		auto* btRoot = juceRmlUi::helper::findChild(_root, _buttonName, false);

		if (!btRoot)
			return false;

		auto* bt = juceRmlUi::helper::findChild(btRoot, "button");

		const auto enabled = _config.getBoolValue(_configName, false);
		juceRmlUi::ElemButton::setChecked(bt, enabled);

		juceRmlUi::EventListener::AddClick(btRoot, [bt, &_config, _changeCallback, _configName]
		{
			auto& c = _config;
			const auto enabled = c.getBoolValue(_configName, false);
			juceRmlUi::ElemButton::setChecked(bt, !enabled);
			c.setValue(_configName, !enabled);
			c.saveIfNeeded();
			_changeCallback(!enabled);
		});

		return true;
	}

	bool SettingsPlugin::createToggleButton(Rml::Element* _root, const std::string& _buttonName, const std::string& _configName, const std::function<void(bool)>& _changeCallback) const
	{
		return createToggleButton(_root, _buttonName, m_processor.getConfig(), _configName, _changeCallback);
	}
}

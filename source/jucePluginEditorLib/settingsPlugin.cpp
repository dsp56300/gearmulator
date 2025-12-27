#include "settingsPlugin.h"

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
}

#include "bulkUninstall.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"

#include <JuceHeader.h>

namespace pluginManager
{
	void confirm(juceRmlUi::RmlComponent& _rmlComponent, const std::string& _title,
		const std::string& _question, const std::shared_ptr<bool>& _alive,
		std::function<void()> _onConfirmed)
	{
		juce::NativeMessageBox::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
			juce::String(_title),
			juce::String(_question),
			nullptr,
			juce::ModalCallbackFunction::create([&_rmlComponent, _alive, onConfirmed = std::move(_onConfirmed)](const int _result)
			{
				if (!_result || !*_alive)
					return;

				// The box answers outside of an RmlUi handler, so the DOM is taken here rather than
				// by everything this calls.
				juceRmlUi::RmlInterfaces::ScopedAccess sa(_rmlComponent);

				onConfirmed();

				_rmlComponent.enqueueUpdate();
			}));
	}

	void confirmUninstall(juceRmlUi::RmlComponent& _rmlComponent, const std::string& _name,
	                      const std::vector<std::string>& _formats, const std::shared_ptr<bool>& _alive,
	                      std::function<void()> _onConfirmed)
	{
		if (_formats.empty())
			return;

		std::string formats;

		for (size_t i = 0; i < _formats.size(); ++i)
			formats += (i ? ", " : "") + _formats[i];

		const auto question = _formats.size() == 1
			? "This deletes " + formats + " from where it is installed."
			: "This deletes " + formats + " from where they are installed.";

		confirm(_rmlComponent, "Remove " + _name + "?", question, _alive, std::move(_onConfirmed));
	}
}

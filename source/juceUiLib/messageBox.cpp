#include "messageBox.h"

#include <memory>

#include "juce_gui_basics/juce_gui_basics.h"

namespace genericUI
{
	namespace
	{
		juce::ModalComponentManager::Callback* addCallback(MessageBox::Callback _callback)
		{
			return juce::ModalCallbackFunction::create([callback = std::move(_callback)](const int _result)
			{
				callback(_result == 1 ? MessageBox::Result::Yes : MessageBox::Result::No);
			});
		}
	}

	void MessageBox::showYesNo(const juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, Callback _callback)
	{
		juce::NativeMessageBox::showYesNoBox(_icon, _header.c_str(), _message.c_str(), nullptr, addCallback(std::move(_callback)));
	}

	void MessageBox::showOkCancel(const juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, Callback _callback)
	{
		juce::NativeMessageBox::showOkCancelBox(_icon, _header.c_str(), _message.c_str(), nullptr, addCallback(std::move(_callback)));
	}

	void MessageBox::showOk(const juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message)
	{
		juce::NativeMessageBox::showMessageBoxAsync(_icon, _header.c_str(), _message.c_str());
	}

	void MessageBox::showOk(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, juce::Component* _owner, std::function<void()> _callback)
	{
		juce::NativeMessageBox::showMessageBoxAsync(_icon, _header.c_str(), _message.c_str(), _owner, 
			juce::ModalCallbackFunction::create([_callback = std::move(_callback)](int)
			{
				_callback();
			}));
	}

	void MessageBox::showOk(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, std::function<void()> _callback)
	{
		return showOk(_icon, _header, _message, nullptr, std::move(_callback));
	}
}

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

		juce::MessageBoxIconType toJuceIcon(const MessageBox::Icon _icon)
		{
			switch (_icon)
			{
				case MessageBox::Icon::None:     return juce::MessageBoxIconType::NoIcon;
				case MessageBox::Icon::Question: return juce::MessageBoxIconType::QuestionIcon;
				case MessageBox::Icon::Warning:  return juce::MessageBoxIconType::WarningIcon;
				case MessageBox::Icon::Info:     return juce::MessageBoxIconType::InfoIcon;
				default:                         return juce::MessageBoxIconType::NoIcon;
			}
		}
	}

	void MessageBox::showYesNo(const Icon _icon, const std::string& _header, const std::string& _message, Callback _callback)
	{
		juce::NativeMessageBox::showYesNoBox(toJuceIcon(_icon), _header.c_str(), _message.c_str(), nullptr, addCallback(std::move(_callback)));
	}

	void MessageBox::showOkCancel(const Icon _icon, const std::string& _header, const std::string& _message, Callback _callback)
	{
		juce::NativeMessageBox::showOkCancelBox(toJuceIcon(_icon), _header.c_str(), _message.c_str(), nullptr, addCallback(std::move(_callback)));
	}

	void MessageBox::showOk(const Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent/* = nullptr*/)
	{
		juce::NativeMessageBox::showMessageBoxAsync(toJuceIcon(_icon), _header.c_str(), _message.c_str(), _associatedComponent);
	}

	void MessageBox::showOk(Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent, std::function<void()> _callback)
	{
		juce::NativeMessageBox::showMessageBoxAsync(toJuceIcon(_icon), _header.c_str(), _message.c_str(), _associatedComponent, 
			juce::ModalCallbackFunction::create([_callback = std::move(_callback)](int)
			{
				_callback();
			}));
	}

	void MessageBox::showOk(Icon _icon, const std::string& _header, const std::string& _message, std::function<void()> _callback)
	{
		return showOk(_icon, _header, _message, nullptr, std::move(_callback));
	}
}

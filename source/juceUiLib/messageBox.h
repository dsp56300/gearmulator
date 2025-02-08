#pragma once

#include <functional>
#include <string>
#include <cstdint>

namespace juce
{
	class Component;
	enum class MessageBoxIconType;
}

namespace genericUI
{
	class MessageBox
	{
	public:
		enum class Result : uint8_t
		{
			Yes = 0,
			No = 1,
			Ok = Yes,
			Cancel = No
		};

		using Callback = std::function<void(Result)>;

		static void showYesNo(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, Callback _callback);
		static void showOkCancel(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, Callback _callback);
		static void showOk(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message);
		static void showOk(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, juce::Component* _owner, std::function<void()> _callback);
		static void showOk(juce::MessageBoxIconType _icon, const std::string& _header, const std::string& _message, std::function<void()> _callback);
	};
}

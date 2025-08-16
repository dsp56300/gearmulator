#pragma once

#include <functional>
#include <string>
#include <cstdint>

namespace juce
{
	class Component;
}

namespace genericUI
{
	class MessageBox
	{
	public:
		enum class Icon : uint8_t
		{
		    None,
		    Question,
		    Warning,
		    Info,
		};

		enum class Result : uint8_t
		{
			Yes = 0,
			No = 1,
			Ok = Yes,
			Cancel = No
		};

		using Callback = std::function<void(Result)>;

		static void showYesNo(Icon _icon, const std::string& _header, const std::string& _message, Callback _callback);
		static void showOkCancel(Icon _icon, const std::string& _header, const std::string& _message, Callback _callback);
		static void showOk(Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent = nullptr);
		static void showOk(Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent, std::function<void()> _callback);
		static void showOk(Icon _icon, const std::string& _header, const std::string& _message, std::function<void()> _callback);
	};
}

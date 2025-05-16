#include "rmlSystemInterface.h"

#include <cassert>

#include "dsp56300/source/dsp56kEmu/logging.h"

namespace juceRmlUi
{
	bool SystemInterface::LogMessage(const Rml::Log::Type _type, const Rml::String& _message)
	{
		switch (_type)
		{
		case Rml::Log::LT_ALWAYS:
			LOG("RML LOG [always]: " << _message.c_str());
			break;
		case Rml::Log::LT_ERROR:
			LOG("RML LOG [error]: " << _message.c_str());
			assert(false && "RML error");
			break;
		case Rml::Log::LT_ASSERT:
			LOG("RML LOG [assert]: " << _message.c_str());
			assert(false && "RML assert");
			break;
		case Rml::Log::LT_WARNING:
			LOG("RML LOG [warning]: " << _message.c_str());
			break;
		case Rml::Log::LT_INFO:
			LOG("RML LOG [info]: " << _message.c_str());
			break;
		case Rml::Log::LT_DEBUG:
			LOG("RML LOG [debug]: " << _message.c_str());
			break;
		case Rml::Log::LT_MAX:
			LOG("RML LOG [MAX]: " << _message.c_str());
			break;
		}
		return Rml::SystemInterface::LogMessage(_type, _message);
	}

	SystemInterface::~SystemInterface()
	= default;

	double SystemInterface::GetElapsedTime()
	{
		return Rml::SystemInterface::GetElapsedTime();
	}

	int SystemInterface::TranslateString(Rml::String& translated, const Rml::String& input)
	{
		return Rml::SystemInterface::TranslateString(translated, input);
	}

	void SystemInterface::JoinPath(Rml::String& translated_path, const Rml::String& document_path,
		const Rml::String& path)
	{
		Rml::SystemInterface::JoinPath(translated_path, document_path, path);
	}

	void SystemInterface::SetMouseCursor(const Rml::String& cursor_name)
	{
		Rml::SystemInterface::SetMouseCursor(cursor_name);
	}

	void SystemInterface::SetClipboardText(const Rml::String& text)
	{
		Rml::SystemInterface::SetClipboardText(text);
	}

	void SystemInterface::GetClipboardText(Rml::String& text)
	{
		Rml::SystemInterface::GetClipboardText(text);
	}

	void SystemInterface::ActivateKeyboard(Rml::Vector2f caret_position, float line_height)
	{
		Rml::SystemInterface::ActivateKeyboard(caret_position, line_height);
	}

	void SystemInterface::DeactivateKeyboard()
	{
		Rml::SystemInterface::DeactivateKeyboard();
	}
}

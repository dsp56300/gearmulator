#include "rmlSystemInterface.h"

#include "dsp56300/source/dsp56kEmu/logging.h"

namespace juceRmlUi
{
	bool SystemInterface::LogMessage(const Rml::Log::Type _type, const Rml::String& _message)
	{
		LOG("RML LOG: " << _type << ": " << _message.c_str());

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

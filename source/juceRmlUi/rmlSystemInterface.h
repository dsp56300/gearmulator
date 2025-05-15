#pragma once

#include "RmlUi/Core/SystemInterface.h"

namespace juceRmlUi
{
	class SystemInterface : public Rml::SystemInterface
	{
		bool LogMessage(Rml::Log::Type _type, const Rml::String& _message) override;

	public:
		~SystemInterface() override;
		double GetElapsedTime() override;
		int TranslateString(Rml::String& translated, const Rml::String& input) override;
		void JoinPath(Rml::String& translated_path, const Rml::String& document_path, const Rml::String& path) override;
		void SetMouseCursor(const Rml::String& cursor_name) override;
		void SetClipboardText(const Rml::String& text) override;
		void GetClipboardText(Rml::String& text) override;
		void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
		void DeactivateKeyboard() override;
	};
}

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
		int TranslateString(Rml::String& _translated, const Rml::String& _input) override;
		void JoinPath(Rml::String& _translatedPath, const Rml::String& _documentPath, const Rml::String& _path) override;
		void SetMouseCursor(const Rml::String& _cursorName) override;
		void SetClipboardText(const Rml::String& _text) override;
		void GetClipboardText(Rml::String& _text) override;
		void ActivateKeyboard(Rml::Vector2f _caretPosition, float _lineHeight) override;
		void DeactivateKeyboard() override;
	};
}

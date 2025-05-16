#pragma once

#include "RmlUi/Core/SystemInterface.h"

namespace juceRmlUi
{
	class SystemInterface : public Rml::SystemInterface
	{
	public:
		explicit SystemInterface() = default;
		SystemInterface(const SystemInterface&) = delete;
		SystemInterface(SystemInterface&&) = delete;
		~SystemInterface() override;

		SystemInterface& operator=(const SystemInterface&) = delete;
		SystemInterface& operator=(SystemInterface&&) = delete;

		double GetElapsedTime() override;
		int TranslateString(Rml::String& _translated, const Rml::String& _input) override;
		void JoinPath(Rml::String& _translatedPath, const Rml::String& _documentPath, const Rml::String& _path) override;
		bool LogMessage(Rml::Log::Type _type, const Rml::String& _message) override;
		void SetMouseCursor(const Rml::String& _cursorName) override;
		void SetClipboardText(const Rml::String& _text) override;
		void GetClipboardText(Rml::String& _text) override;
		void ActivateKeyboard(Rml::Vector2f _caretPosition, float _lineHeight) override;
		void DeactivateKeyboard() override;
	};
}

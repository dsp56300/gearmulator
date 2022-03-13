#pragma once

#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

namespace Virus
{
	class Controller;
}

class VirusParameterBinding;

namespace genericUI
{
	class UiObject;

	class Editor : public juce::Component
	{
	public:
		explicit Editor(const std::string& _json, VirusParameterBinding& _binding, Virus::Controller& _controller);

		juce::Drawable* getImageDrawable(const std::string& _texture);
		std::unique_ptr<juce::Drawable> createImageDrawable(const std::string& _texture);

		VirusParameterBinding& getParameterBinding() const { return m_parameterBinding; }
		Virus::Controller& getController() const { return m_controller; }

	private:
		static const char* getResourceByFilename(const std::string& _filename, int& _outDataSize);

		std::map<std::string, std::unique_ptr<juce::Drawable>> m_drawables;
		std::unique_ptr<UiObject> m_rootObject;

		VirusParameterBinding& m_parameterBinding;
		Virus::Controller& m_controller;
	};
}

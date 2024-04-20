#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace pluginLib
{
	class Controller;
	class Parameter;
}

namespace genericVirusUI
{
	class VirusEditor;

	class ArpUserPattern : public juce::Component
	{
	public:
		ArpUserPattern(const VirusEditor& _editor);

		void paint(juce::Graphics& g) override;

		void onCurrentPartChanged();
	private:
		void unbindParameters();
		void bindParameters();
		static void unbindParameter(pluginLib::Parameter*& _parameter);

		pluginLib::Parameter* bindParameter(const std::string& _name);

		void onParameterChanged();

		struct Step
		{
			pluginLib::Parameter* length = nullptr;
			pluginLib::Parameter* velocity = nullptr;
			pluginLib::Parameter* bitfield = nullptr;
		};

		pluginLib::Controller& m_controller;
		std::array<Step, 32> m_steps;
		pluginLib::Parameter* m_patternLength = nullptr;

		juce::Colour m_colRectFillActive = juce::Colour(0xddffffff);
		juce::Colour m_colRectFillInactive = juce::Colour(0x77aaaaaa);
		float m_gradientStrength = 0.0f;
	};
}

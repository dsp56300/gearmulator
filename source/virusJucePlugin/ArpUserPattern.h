#pragma once

#include "jucePluginLib/parameterlistener.h"

#include "juceRmlUi/rmlElemCanvas.h"

#include "juce_graphics/juce_graphics.h"

namespace pluginLib
{
	class Parameter;
	class Controller;
}

namespace genericVirusUI
{
	class VirusEditor;

	class ArpUserPattern
	{
	public:
		using BoundParam = std::pair<pluginLib::Parameter*, pluginLib::ParameterListener>;

		ArpUserPattern(const VirusEditor& _editor, Rml::Element* _parent);

		void paint(const juce::Image& _image, juce::Graphics& g);

		void onCurrentPartChanged();
	private:
		void unbindParameters();
		void bindParameters();
		static void unbindParameter(BoundParam& _parameter);

		BoundParam bindParameter(const std::string& _name);

		void onParameterChanged();

		struct Step
		{
			BoundParam length;
			BoundParam velocity;
			BoundParam bitfield;
		};

		pluginLib::Controller& m_controller;
		std::array<Step, 32> m_steps;
		BoundParam m_patternLength;

		juce::Colour m_colRectFillActive = juce::Colour(0xddffffff);
		juce::Colour m_colRectFillInactive = juce::Colour(0x77aaaaaa);
		float m_gradientStrength = 0.0f;
		juceRmlUi::ElemCanvas* m_canvas;
	};
}

#pragma once

#include "baseLib/event.h"

#include <vector>
#include <cstdint>

namespace Rml
{
	class Element;
}

namespace xtJucePlugin
{
	class Editor;

	class Arp
	{
	public:
		explicit Arp(Editor& _editor);

	private:
		void bind() const;
		void bind(Rml::Element* _component, const char** _bindings) const;

		Editor& m_editor;

		Rml::Element* m_arpMode;
		Rml::Element* m_arpClock;
		Rml::Element* m_arpPattern;
		Rml::Element* m_arpDirection;
		Rml::Element* m_arpOrder;
		Rml::Element* m_arpVelocity;
		Rml::Element* m_arpTempo;
		Rml::Element* m_arpRange;
		std::vector<Rml::Element*> m_arpReset;

		baseLib::EventListener<bool> m_onPlayModeChanged;
		baseLib::EventListener<uint8_t> m_onPartChanged;
	};
}

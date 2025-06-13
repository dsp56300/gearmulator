#pragma once

#include "rmlElement.h"
#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class ElemSplitter final : public Element, Rml::EventListener
	{
	public:
		explicit ElemSplitter(const Rml::String& _tag);

		void ProcessEvent(Rml::Event& _event) override;

	private:
		void processMouseMove(Rml::Vector2f _mousePos);

		Rml::Vector2f m_lastMousePos;
	};
}

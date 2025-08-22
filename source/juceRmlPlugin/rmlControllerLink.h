#pragma once

#include "RmlUi/Core/EventListener.h"

namespace Rml
{
	class Element;
}

namespace rmlPlugin
{
	class ControllerLink : Rml::EventListener
	{
	public:
		ControllerLink(Rml::Element* _source, Rml::Element* _target, Rml::Element* _conditionButton);
		~ControllerLink() override;

		void ProcessEvent(Rml::Event& _event) override;

		Rml::Element* getSource() const { return m_source; }
		Rml::Element* getTarget() const { return m_target; }
		Rml::Element* getConditionButton() const { return m_conditionButton; }

		static float getValue(Rml::Element* _element);
		static void setValue(Rml::Element* _element, float _value);

	private:
		Rml::Element* m_source = nullptr;
		Rml::Element* m_target = nullptr;
		Rml::Element* m_conditionButton = nullptr;
		bool m_sourceIsBeingDragged = false;
		float m_lastSourceValue = 0.0f;
		bool m_processingChangeEvent = false;
	};
}

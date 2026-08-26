#pragma once

#include <set>

#include "jucePluginLib/parameterlistener.h"

#include "RmlUi/Core/EventListener.h"

namespace Rml
{
	class Element;
}

namespace pluginLib
{
	class Controller;
	class Parameter;
}

namespace rmlPlugin
{
	class RmlParameterBinding;

	class ParameterToElementsBinding : Rml::EventListener
	{
	public:
		explicit ParameterToElementsBinding(RmlParameterBinding& _binding, pluginLib::Parameter* _parameter, Rml::Element* _firstElement);

		ParameterToElementsBinding(const ParameterToElementsBinding&) = delete;
		ParameterToElementsBinding(ParameterToElementsBinding&&) = delete;

		ParameterToElementsBinding& operator=(const ParameterToElementsBinding&) = delete;
		ParameterToElementsBinding& operator=(ParameterToElementsBinding&&) = delete;

		~ParameterToElementsBinding() override;

		bool empty() const { return m_elements.empty(); }

		bool addElement(Rml::Element* _element);
		bool removeElement(Rml::Element* _element);

		pluginLib::Parameter* getParameter() const { return m_parameter; }
		std::set<Rml::Element*> getElements() const { return m_elements; }

	private:
		void onParameterValueChanged();

		void updateElementsFromParameter();

		static void setElementParameterDefaults(Rml::Element* _element, const pluginLib::Parameter* _parameter);
		void setElementsParameterDefaults(const pluginLib::Parameter* _parameter) const;

		void ProcessEvent(Rml::Event& _event) override;
		void OnDetach(Rml::Element*) override;

		static int getElementValue(const Rml::Element* _element, const pluginLib::Parameter* _default);
		static void setElementValue(Rml::Element* _element, const pluginLib::Parameter* _source);
		static bool isReversed(const Rml::Element* _element);

		RmlParameterBinding& m_binding;
		pluginLib::Parameter* const m_parameter;
		pluginLib::ParameterListener m_listener;
		std::set<Rml::Element*> m_elements;

		class IgnoreChangeEvents
		{
		public:
			explicit IgnoreChangeEvents(ParameterToElementsBinding& _owner);

			~IgnoreChangeEvents();

		private:
			ParameterToElementsBinding& m_owner;
		};
		uint32_t m_ignoreChangeEvents = 0;

		baseLib::EventListener<pluginLib::Parameter*> m_onSoftKnobTargetChanged;
	};
}

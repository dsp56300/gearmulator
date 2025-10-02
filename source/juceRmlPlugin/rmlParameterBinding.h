#pragma once

#include <cstdint>

#include "rmlParameterRef.h"
#include "rmlParameterToElementsBinding.h"

#include "jucePluginLib/controller.h"

#include "juceRmlUi/rmlInterfaces.h"

namespace juceRmlUi
{
	class RmlComponent;
}

namespace Rml
{
	class Context;
}

namespace pluginLib
{
	class ParameterDescriptions;
}

namespace rmlPlugin
{
	class RmlParameterBinding
	{
	public:
		baseLib::Event<pluginLib::Parameter*, Rml::Element*> evBind;
		baseLib::Event<pluginLib::Parameter*, Rml::Element*> evUnbind;

		static constexpr uint8_t CurrentPart = pluginLib::MidiPacket::AnyPart;

		explicit RmlParameterBinding(pluginLib::Controller& _controller, Rml::Context* _context, juceRmlUi::RmlComponent& _component);
		RmlParameterBinding(const RmlParameterBinding&) = delete;
		RmlParameterBinding(RmlParameterBinding&&) = delete;
		~RmlParameterBinding();

		static std::string getDataModelName(uint8_t _part);
		static uint8_t getPartFromDataModelName(const std::string& _name);

		void bindParametersForPart(Rml::Context* _context, uint8_t _targetPart, uint8_t _sourcePart);
		void bindParameters(Rml::Context* _context, uint8_t _partCount);

		juceRmlUi::RmlComponent& getRmlComponent() const { return m_component; }
		Rml::CoreInstance& getCoreInstance() const;

		RmlParameterBinding& operator=(const RmlParameterBinding&) = delete;
		RmlParameterBinding& operator=(RmlParameterBinding&&) = delete;

		bool bind(Rml::Element& _element, const std::string& _parameterName);
		void bind(Rml::Element& _element, const std::string& _parameterName, uint8_t _part);

		void unbind(Rml::Element& _element);

		void getElementsForParameter(std::vector<Rml::Element*>& _results, const std::string& _param, uint8_t _part = 0, bool _visibleOnly = true) const;
		void getElementsForParameter(std::vector<Rml::Element*>& _results, const pluginLib::Parameter* _param, bool _visibleOnly = true) const;
		Rml::Element* getElementForParameter(const pluginLib::Parameter* _param, bool _visibleOnly = true) const;
		Rml::Element* getElementForParameter(const std::string& _param, uint8_t _part, bool _visibleOnly = true) const;
		const pluginLib::Parameter* getParameterForElement(const Rml::Element* _element) const;

		void setMouseIsDown(Rml::ElementDocument* _document, bool _isDown);
		bool getMouseIsDown() const { return !m_docsWithMouseDown.empty(); }

		void registerPendingGesture(pluginLib::Parameter* _param);

		auto& getController() const { return m_controller; }

	private:
		void releasePendingGestures();

		void setCurrentPart(uint8_t _part);

		using ParameterList = std::vector<RmlParameterRef>;

		pluginLib::Controller& m_controller;
		juceRmlUi::RmlComponent& m_component;
		Rml::Context* const m_context;

		std::array<ParameterList, CurrentPart + 1> m_parametersPerPart;
		baseLib::EventListener<uint8_t> m_onCurrentPartChanged;

		std::unordered_map<Rml::Element*, ParameterToElementsBinding*> m_elementToParam;
		std::unordered_map<pluginLib::Parameter*, ParameterToElementsBinding*> m_paramToElements;
		std::set<Rml::Element*> m_elementsBoundToCurrentPart;

		std::unordered_set<Rml::ElementDocument*> m_docsWithMouseDown;

		std::set<pluginLib::Parameter*> m_pendingGestures;
	};
}

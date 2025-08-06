#pragma once

#include <cstdint>

#include "rmlParameterRef.h"

#include "jucePluginLib/controller.h"

#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/DataModelHandle.h"

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
		static constexpr uint8_t CurrentPart = pluginLib::MidiPacket::AnyPart;

		explicit RmlParameterBinding(pluginLib::Controller& _controller, Rml::Context* _context);
		RmlParameterBinding(const RmlParameterBinding&) = delete;
		RmlParameterBinding(RmlParameterBinding&&) = default;
		~RmlParameterBinding() = default;

		void bindParametersForPart(Rml::Context* _context, uint8_t _targetPart, uint8_t _sourcePart);
		void bindParameters(Rml::Context* _context, uint8_t _partCount);

		juceRmlUi::RmlComponent& getRmlComponent() const { return m_component; }

		RmlParameterBinding& operator=(const RmlParameterBinding&) = delete;
		RmlParameterBinding& operator=(RmlParameterBinding&&) = delete;

		void bind(Rml::Element& _element, const std::string& _parameterName, uint8_t _part) const;
		static void bind(const pluginLib::Controller& m_controller, Rml::Element& _element, const std::string& _parameterName, uint8_t _part);

	private:
		void setCurrentPart(uint8_t _part);

		using ParameterList = std::vector<RmlParameterRef>;

		const pluginLib::Controller& m_controller;
		juceRmlUi::RmlComponent& m_component;
		std::array<ParameterList, CurrentPart + 1> m_parametersPerPart;
		baseLib::EventListener<uint8_t> m_onCurrentPartChanged;
	};
}

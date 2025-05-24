#include "rmlPlugin.h"

#include "rmlParameterBinding.h"
#include "RmlUi/Core/Core.h"

namespace rmlPlugin
{
	RmlPlugin::RmlPlugin(pluginLib::Controller& _controller) : m_controller(_controller)
	{
		Rml::RegisterPlugin(this);
	}
	void RmlPlugin::OnContextCreate(Rml::Context* _context)
	{
		juce::MessageManagerLock lock;
		m_bindings.emplace(_context, std::make_unique<RmlParameterBinding>(m_controller, _context));
	}

	void RmlPlugin::OnContextDestroy(Rml::Context* _context)
	{
		const auto it = m_bindings.find(_context);
		if (it == m_bindings.end())
		{
			assert(false && "RmlPlugin::OnContextDestroy: Context not found in bindings map");
			return;
		}
		m_bindings.erase(it);

		Plugin::OnContextDestroy(_context);
	}
	
	void RmlPlugin::OnElementCreate(Rml::Element* _element)
	{
		Plugin::OnElementCreate(_element);

		auto* attrib = _element->GetAttribute("param");

		if (!attrib)
			return;

		const auto param = RmlParameterRef::createVariableName(attrib->Get<Rml::String>());

		_element->SetAttribute("data-attr-min", param + "_min");
		_element->SetAttribute("data-attr-max", param + "_max");
		_element->SetAttribute("data-value", param + "_value");
	}

}

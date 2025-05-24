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
}

#include "rmlPlugin.h"

#include "rmlParameterBinding.h"
#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace rmlPlugin
{
	RmlPlugin::RmlPlugin(Rml::CoreInstance& _coreInstance, pluginLib::Controller& _controller) : m_coreInstance(_coreInstance), m_controller(_controller)
	{
		Rml::RegisterPlugin(m_coreInstance, this);
	}

	RmlPlugin::~RmlPlugin()
	{
		Rml::UnregisterPlugin(m_coreInstance, this);
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

		m_tabGroups.clear();

		Plugin::OnContextDestroy(_context);
	}
	
	void RmlPlugin::OnElementCreate(Rml::Element* _element)
	{
		Plugin::OnElementCreate(_element);

		if (const auto* attribParam = _element->GetAttribute("param"))
		{
			// TODO: which part?
			RmlParameterBinding::bind(m_controller, *_element, attribParam->Get<Rml::String>(_element->GetCoreInstance()), 0);
		}

		if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(_element))
		{
			// placeholder for input elements, only valid if an input of type text has exacly one child.
			// if there is a placeholder, it is only shown when the input is empty.
//			if (input->GetNumChildren() == 1)
			{
				auto type = input->GetAttribute("type");

				if (type && type->Get<Rml::String>(input->GetCoreInstance()) == "text")
				{
					juceRmlUi::EventListener::Add(input, Rml::EventId::Change, [input](const Rml::Event&)
					{
						const auto isEmpty = input->GetValue().empty();
						if (auto child = input->GetFirstChild())
						{
							child->SetProperty(Rml::PropertyId::Visibility, Rml::Property(isEmpty ? Rml::Style::Visibility::Visible :  Rml::Style::Visibility::Hidden));
						}
					});
				}
			}
		}

		if (auto* attribTabGroup = _element->GetAttribute("tabgroup"))
		{
			const auto name = attribTabGroup->Get<Rml::String>(_element->GetCoreInstance());
			auto& tabGroup = m_tabGroups[name];
			if (!tabGroup)
				tabGroup = std::make_unique<TabGroup>();
			if (auto* attribPage = _element->GetAttribute("tabpage"))
				tabGroup->setPage(_element, std::stoi(attribPage->Get<Rml::String>(_element->GetCoreInstance())));
			else if (auto* attribButton = _element->GetAttribute("tabbutton"))
				tabGroup->setButton(_element, std::stoi(attribButton->Get<Rml::String>(_element->GetCoreInstance())));
			else
				throw std::runtime_error("tabgroup element must have either tabpage or tabbutton attribute set");
		}
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	RmlParameterBinding* RmlPlugin::getParameterBinding(Rml::Context* _context)
	{
		auto it = m_bindings.find(_context);
		if (it != m_bindings.end())
			return it->second.get();
		return nullptr;
	}
}

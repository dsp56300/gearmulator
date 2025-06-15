#include "rmlPlugin.h"

#include "rmlParameterBinding.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace rmlPlugin
{
	RmlPlugin::RmlPlugin(pluginLib::Controller& _controller) : m_controller(_controller)
	{
		Rml::RegisterPlugin(this);
	}

	RmlPlugin::~RmlPlugin()
	{
		Rml::UnregisterPlugin(this);
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

		if (attrib)
		{
			const auto name = attrib->Get<Rml::String>();
			const auto param = RmlParameterRef::createVariableName(name);

			_element->SetAttribute("data-attr-min", param + "_min");
			_element->SetAttribute("data-attr-max", param + "_max");
			_element->SetAttribute("data-value", param + "_value");

			if (auto* combo = dynamic_cast<juceRmlUi::ElemComboBox*>(_element))
			{
				std::vector<Rml::String> options;
				auto* p = m_controller.getParameter(name, 0);	// TODO: which part?

				if (!p)
				{
					std::stringstream ss;
					ss << "Failed to find parameter " << name << " for combo box";
					Rml::Log::Message(Rml::Log::LT_ERROR, ss.str().c_str());
					return;
				}

				combo->setOptions(p->getDescription().valueList.texts);
			}
		}

		if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(_element))
		{
			// placeholder for input elements, only valid if an input of type text has exacly one child.
			// if there is a placeholder, it is only shown when the input is empty.
//			if (input->GetNumChildren() == 1)
			{
				auto type = input->GetAttribute("type");

				if (type && type->Get<Rml::String>() == "text")
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
	}
}

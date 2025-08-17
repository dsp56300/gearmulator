#include "rmlPlugin.h"

#include "rmlControllerLink.h"
#include "rmlParameterBinding.h"
#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
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

		m_lastCreatedContext = _context;
	}

	void RmlPlugin::OnContextDestroy(Rml::Context* _context)
	{
		if (m_lastCreatedContext == _context)
			m_lastCreatedContext = nullptr;

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

		bindElementParameter(_element);

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

		if (auto* attribLinkTarget = _element->GetAttribute("controllerLinkTarget"))
		{
			const auto target = attribLinkTarget->Get<Rml::String>(_element->GetCoreInstance());
			if (target.empty())
				throw std::runtime_error("controllerLinkTarget attribute must not be empty");

			std::string conditionButton = _element->GetAttribute("controllerLinkCondition", std::string());
			if (conditionButton.empty())
				throw std::runtime_error("controllerLinkCondition attribute must not be empty");

			m_controllerLinkDescs.push_back({ _element, target, conditionButton });
		}
	}

	void RmlPlugin::OnDocumentLoad(Rml::ElementDocument* _document)
	{
		Plugin::OnDocumentLoad(_document);

		for (const auto & desc : m_controllerLinkDescs)
		{
			auto* target = juceRmlUi::helper::findChild(_document, desc.target, true);
			auto* button = juceRmlUi::helper::findChild(_document, desc.conditionButton, true);

			m_controllerLinks.emplace_back(std::make_unique<ControllerLink>(desc.source, target, button));
		}

		m_controllerLinkDescs.clear();
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	RmlParameterBinding* RmlPlugin::getParameterBinding(Rml::Context* _context)
	{
		auto it = m_bindings.find(_context);
		if (it != m_bindings.end())
			return it->second.get();
		return nullptr;
	}

	void RmlPlugin::bindElementParameter(Rml::Element* _element)
	{
		const auto* attribParam = _element->GetAttribute("param");
		if (!attribParam)
			return;

		Rml::Context* context = _element->GetContext();
		if (!context)
		{
			// this is highly problematic: For elements newly created, there might be no way to know which context this element
			// belongs to because the context is still null. If there is one binding we are safe, if there are multiple bindings
			// we have to assume the last created context is the one this element belongs to.
			if (m_bindings.size() == 1)
				context = m_bindings.begin()->first;
			else if (m_lastCreatedContext)
				context = m_lastCreatedContext;
			else
			{
				assert(false);
				Rml::Log::Message(Rml::Log::LT_ERROR, "RmlPlugin::OnElementCreate: Element '%s' has a 'param' attribute but failed to determine which context this element belongs to", _element->GetId().c_str());
				return;
			}
		}
		const auto it = m_bindings.find(context);

		if (it == m_bindings.end())
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "RmlPlugin::OnElementCreate: Context not found in bindings map");
			return;
		}

		it->second->bind(*_element, attribParam->Get<Rml::String>(_element->GetCoreInstance()));
	}
}

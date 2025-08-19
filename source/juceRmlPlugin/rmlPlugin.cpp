#include "rmlPlugin.h"

#include "rmlParameterBinding.h"
#include "rmlPluginContext.h"
#include "rmlPluginDocument.h"
#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "RmlUi/Core/Context.h"

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
		m_contexts.emplace(_context, std::make_unique<RmlPluginContext>(_context, m_controller));
	}

	void RmlPlugin::OnContextDestroy(Rml::Context* _context)
	{
		m_documentBeingLoaded.reset();

		const auto it = m_contexts.find(_context);
		if (it == m_contexts.end())
		{
			assert(false && "RmlPlugin::OnContextDestroy: Context not found");
			return;
		}

		m_contexts.erase(it);

		Plugin::OnContextDestroy(_context);
	}

	void RmlPlugin::OnElementCreate(Rml::Element* _element)
	{
		Plugin::OnElementCreate(_element);

		auto* context = _element->GetContext();

		if (m_documentBeingLoaded)
		{
			m_documentBeingLoaded->elementCreated(_element);
			if (!context)
				context = m_documentBeingLoaded->getContext();
		}

		if (!context)
		{
			if (m_contexts.size() == 1)
				context = m_contexts.begin()->second->getContext();
			else
				return;
		}

		const auto it = m_contexts.find(context);

		if (it == m_contexts.end())
		{
			Rml::Log::Message(_element->GetCoreInstance(), Rml::Log::LT_ERROR, "RmlPlugin::OnElementCreate: Context not found in bindings map");
			return;
		}

		it->second->elementCreated(_element);

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
	}

	void RmlPlugin::OnDocumentOpen(Rml::Context* _context, const Rml::String& _documentPath)
	{
		auto it = m_contexts.find(_context);

		if (it == m_contexts.end())
		{
			Rml::Log::Message(_context->GetCoreInstance(), Rml::Log::LT_ERROR, "RmlPlugin::OnDocumentOpen: Context not found");
			return;
		}

		m_documentBeingLoaded = std::make_unique<RmlPluginDocument>(*it->second);

		Plugin::OnDocumentOpen(_context, _documentPath);
	}

	void RmlPlugin::OnDocumentLoad(Rml::ElementDocument* _document)
	{
		Plugin::OnDocumentLoad(_document);

		if (!m_documentBeingLoaded)
			return;	// This happens if the debug document is created which is not loaded before but created at runtime

		const auto it = m_contexts.find(_document->GetContext());
		assert(it != m_contexts.end());
		RmlPluginContext* pluginContext = it->second.get();

		m_documentBeingLoaded->loadCompleted(_document);

		pluginContext->addDocument(std::move(m_documentBeingLoaded));

		m_documentBeingLoaded.reset();
	}

	void RmlPlugin::OnDocumentUnload(Rml::ElementDocument* _document)
	{
		auto it = m_contexts.find(_document->GetContext());
		if (it == m_contexts.end())
		{
			Rml::Log::Message(_document->GetCoreInstance(), Rml::Log::LT_ERROR, "RmlPlugin::OnDocumentUnload: Context not found");
			return;
		}
		RmlPluginContext* pluginContext = it->second.get();
		pluginContext->removeDocument(_document);
		Plugin::OnDocumentUnload(_document);
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	RmlParameterBinding* RmlPlugin::getParameterBinding(Rml::Context* _context)
	{
		auto it = m_contexts.find(_context);
		if (it != m_contexts.end())
			return &it->second->getParameterBinding();
		return nullptr;
	}

	bool RmlPlugin::selectTabWithElement(const Rml::Element* _element)
	{
		const auto it = m_contexts.find(_element->GetContext());
		if (it == m_contexts.end())
			return false;
		return it->second->selectTabWithElement(_element);
	}
}

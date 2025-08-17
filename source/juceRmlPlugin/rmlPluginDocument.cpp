#include "rmlPluginDocument.h"

#include "rmlControllerLink.h"
#include "rmlPluginContext.h"
#include "rmlTabGroup.h"

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/ElementDocument.h"

namespace rmlPlugin
{
	RmlPluginDocument::RmlPluginDocument(RmlPluginContext& _context) : m_context(_context)
	{
	}

	RmlPluginDocument::~RmlPluginDocument()
	{
		m_tabGroups.clear();
		m_controllerLinks.clear();
		m_controllerLinkDescs.clear();
	}

	Rml::Context* RmlPluginDocument::getContext() const
	{
		return m_context.getContext();
	}

	void RmlPluginDocument::loadCompleted(Rml::ElementDocument* _doc)
	{
		m_document = _doc;

		for (const auto & desc : m_controllerLinkDescs)
		{
			auto* target = juceRmlUi::helper::findChild(m_document, desc.target, true);
			auto* button = juceRmlUi::helper::findChild(m_document, desc.conditionButton, true);

			m_controllerLinks.emplace_back(std::make_unique<ControllerLink>(desc.source, target, button));
		}

		m_controllerLinkDescs.clear();

	}

	void RmlPluginDocument::elementCreated(Rml::Element* _element)
	{
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
}

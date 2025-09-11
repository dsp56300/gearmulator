#include "treeNode.h"

#include "patchmanagerUiRml.h"
#include "tree.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginEditorLib/patchmanager/patchmanagerui.h"
#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

#include "juceRmlUi/rmlDragData.h"
#include "juceRmlUi/rmlEventListener.h"

namespace jucePluginEditorLib::patchManagerRml
{
	TreeElem::TreeElem(Tree& _tree, Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : ElemTreeNode(_coreInstance, _tag), m_treeRef(_tree)
	{
		DragSource::init(this);
		DragTarget::init(this);

		setAllowLocations(false, false);
	}

	PatchManagerUiRml& TreeElem::getPatchManager() const
	{
		return m_treeRef.getPatchManager();
	}

	patchManager::PatchManager& TreeElem::getDB() const
	{
		return getPatchManager().getDB();
	}

	void TreeElem::cancelSearch()
	{
		if(m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return;

		getDB().cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	void TreeElem::search(pluginLib::patchDB::SearchRequest&& _request)
	{
		cancelSearch();
		setCount(g_unknownCount);
		m_searchRequest = _request;
		m_searchHandle = getDB().search(std::move(_request));
	}

	void TreeElem::processSearchUpdated(const pluginLib::patchDB::Search& _search)
	{
		setCount(_search.getResultSize());
	}

	void TreeElem::setParentSearchRequest(const pluginLib::patchDB::SearchRequest& _parentSearch)
	{
		if(_parentSearch == m_parentSearchRequest)
			return;
		m_parentSearchRequest = _parentSearch;
		onParentSearchChanged(m_parentSearchRequest);
	}

	void TreeElem::setName(const std::string& _name, const bool _forceUpdate/* = false*/)
	{
		if (!_forceUpdate && m_name == _name)
			return;

		m_name = _name;

		if (!m_elemName)
			return;

		m_elemName->SetInnerRML(Rml::StringUtilities::EncodeRml(_name));
	}

	void TreeElem::setColor(const uint32_t _color, const bool _forceUpdate)
	{
		if (!_forceUpdate && m_color == _color)
			return;

		m_color = _color;

		if (m_color == pluginLib::patchDB::g_invalidColor)
		{
			m_elemName->RemoveProperty(Rml::PropertyId::Color);
		}
		else
		{
			const Rml::Colourb c = PatchManagerUiRml::toRmlColor(m_color);

			m_elemName->SetProperty(Rml::PropertyId::Color, Rml::Property(c, Rml::Unit::COLOUR));
		}
	}

	void TreeElem::setCount(const size_t _count, const bool _forceUpdate/* = false*/)
	{
		if (!_forceUpdate && m_count == _count)
			return;

		m_count = _count;

		if (!m_elemCount)
			return;

		if (!m_countEnabled)
		{
			m_elemCount->SetInnerRML("");
			return;
		}

		if (m_count == g_unknownCount)
		{
			m_elemCount->SetInnerRML(Rml::StringUtilities::EncodeRml(m_countUnknown));
		}
		else
		{
			const auto count = std::to_string(m_count);
			char temp[32];
			(void)snprintf(temp, sizeof(temp)-1, m_countFormat.c_str(), count.c_str());  // NOLINT(clang-diagnostic-format-nonliteral)
			m_elemCount->SetInnerRML(Rml::StringUtilities::EncodeRml(temp));
		}
	}

	void TreeElem::setCountEnabled(const bool _enabled)
	{
		if (m_countEnabled == _enabled)
			return;
		m_countEnabled = _enabled;
		setCount(m_count, true);
	}

	void TreeElem::OnChildAdd(Rml::Element* _child)
	{
		Rml::Element::OnChildAdd(_child);

		if (m_elemName == nullptr)
		{
			if (_child->GetId() == "name")
			{
				m_elemName = _child;
				setName(m_name, true);
				setColor(m_color, true);
			}
		}
		if (m_elemCount == nullptr)
		{
			if (_child->GetId() == "count")
			{
				m_elemCount = _child;

				auto attrib = m_elemCount->GetAttribute("countFormat");
				if (attrib)
					m_countFormat = attrib->Get<Rml::String>(GetCoreInstance());
				if (m_countFormat.empty())
					m_countFormat = " (%s)";

				attrib = m_elemCount->GetAttribute("countUnknown");
				if (attrib)
					m_countUnknown = attrib->Get<Rml::String>(GetCoreInstance());
				if (m_countUnknown.empty())
					m_countUnknown = " (?)";

				setCount(m_count, true);
			}
		}

		if (m_elemAdd == nullptr)
		{
			if (_child->GetId() == "add")
			{
				m_elemAdd = _child;

				setCanAdd(m_canAdd);

				juceRmlUi::EventListener::Add(m_elemAdd, Rml::EventId::Click, [this](Rml::Event& _event)
				{
					onAddPressed(_event);
				});
			}
		}
	}

	void TreeElem::processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _searches)
	{
		if (_searches.find(m_searchHandle) == _searches.end())
			return;

		const auto search = getDB().getSearch(m_searchHandle);
		if (!search)
			return;

		processSearchUpdated(*search);
	}

	void TreeElem::onClick()
	{
		ElemTreeNode::onClick();
	}

	std::unique_ptr<juceRmlUi::DragData> TreeElem::createDragData()
	{
		return {};
	}

	bool TreeElem::canDrop(const Rml::Event& _event, const DragSource* _source)
	{
		auto patches = patchManager::SavePatchDesc::getPatchesFromDragSource(*_source);
		if (patches.empty())
			return DragTarget::canDrop(_event, _source);
		return canDropPatchList(_event, _source->getElement(), patches);
	}

	void TreeElem::drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data)
	{
		auto savePatchDesc = dynamic_cast<const patchManager::SavePatchDesc*>(_data);
		if (!savePatchDesc || !savePatchDesc->hasPatches())
			return DragTarget::drop(_event, _source, _data);
		
		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& it : savePatchDesc->getPatches())
			patches.push_back(it.second);

		dropPatches(_event, savePatchDesc, patches);
	}

	void TreeElem::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		const auto patches = getDB().loadPatchesFromFiles(_files);

		if (!patches.empty())
			dropPatches(_event, dynamic_cast<const patchManager::SavePatchDesc*>(_data), patches);
	}

	void TreeElem::onAddPressed(Rml::Event& _event)
	{
		onRightClick(_event);
	}

	void TreeElem::setCanAdd(const bool _enable, const std::string& _image/* = {}*/)
	{
		m_canAdd = _enable;

		if (m_elemAdd)
		{
			if (_enable)
				m_elemAdd->RemoveProperty(Rml::PropertyId::Display);
			else
				m_elemAdd->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));

			m_elemAdd->SetAttribute("src", _image);
		}
	}
}

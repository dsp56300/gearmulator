#pragma once

#include "jucePluginLib/patchdb/patchdbtypes.h"
#include "jucePluginLib/patchdb/search.h"

#include "juceRmlUi/rmlElemTreeNode.h"

#include <limits>

namespace pluginLib::patchDB
{
	struct Search;
	struct SearchRequest;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml;
	class Tree;

	class TreeElem : public juceRmlUi::ElemTreeNode
	{
	public:
		static constexpr size_t g_unknownCount = std::numeric_limits<size_t>::max();

		explicit TreeElem(Tree& _tree, const Rml::String& _tag);

		PatchManagerUiRml& getPatchManager() const;
		patchManager::PatchManager& getDB() const;

		void cancelSearch();
		void search(pluginLib::patchDB::SearchRequest&& _request);

		virtual void processSearchUpdated(const pluginLib::patchDB::Search& _search);
		virtual void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest) {}

		const pluginLib::patchDB::SearchRequest& getParentSearchRequest() const { return m_parentSearchRequest; }

		void setParentSearchRequest(const pluginLib::patchDB::SearchRequest& _parentSearch);
		auto& getSearchRequest() { return m_searchRequest; }
		pluginLib::patchDB::SearchHandle getSearchHandle() const { return m_searchHandle; }

		void setName(const std::string& _name, bool _forceUpdate = false);
		void setColor(uint32_t _color, bool _forceUpdate = false);
		void setCount(size_t _count, bool _forceUpdate = false);
		void setCountEnabled(bool _enabled);

		void OnChildAdd(Rml::Element* _child) override;

		void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _searches);

		void onClick() override;

	private:
		Tree& m_treeRef;

		pluginLib::patchDB::SearchRequest m_parentSearchRequest;
		pluginLib::patchDB::SearchRequest m_searchRequest;

		uint32_t m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;

		std::string m_name;
		uint32_t m_color = pluginLib::patchDB::g_invalidColor;
		size_t m_count = g_unknownCount;

		Rml::Element* m_elemName = nullptr;
		Rml::Element* m_elemCount = nullptr;

		std::string m_countFormat;
		std::string m_countUnknown;

		bool m_countEnabled = true;
	};
}

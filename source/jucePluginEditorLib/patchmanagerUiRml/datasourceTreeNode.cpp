#include "datasourceTreeNode.h"

#include "datasourceTree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	namespace
	{
		std::string getDataSourceTitle(const pluginLib::patchDB::DataSource& _ds)
		{
			switch (_ds.type)
			{
			case pluginLib::patchDB::SourceType::Invalid:
			case pluginLib::patchDB::SourceType::Count:
				return {};
			case pluginLib::patchDB::SourceType::Rom:
			case pluginLib::patchDB::SourceType::LocalStorage:
				return _ds.name;
			case pluginLib::patchDB::SourceType::File:
			case pluginLib::patchDB::SourceType::Folder:
				{
					auto n = _ds.name;
					const auto idxA = n.find_first_of("\\/");
					const auto idxB = n.find_last_of("\\/");
					if(idxA != std::string::npos && idxB != std::string::npos && (idxB - idxA) > 1)
					{
						return n.substr(0, idxA+1) + ".." + n.substr(idxB);
					}
					return n;
				}
			default:
				assert(false);
				return"invalid";
			}
		}

		std::string getDataSourceNodeTitle(const pluginLib::patchDB::DataSourceNodePtr& _ds)
		{
			if (_ds->origin == pluginLib::patchDB::DataSourceOrigin::Manual)
				return getDataSourceTitle(*_ds);

			auto t = getDataSourceTitle(*_ds);
			const auto pos = t.find_last_of("\\/");
			if (pos != std::string::npos)
				return t.substr(pos + 1);
			return t;
		}
	}

	std::string DatasourceNode::getText() const
	{
		return m_ds ? getDataSourceNodeTitle(m_ds) : std::string();
	}

	DatasourceTreeElem::DatasourceTreeElem(Tree& _tree, const Rml::String& _tag) : TreeElem(_tree, _tag)
	{
	}

	void DatasourceTreeElem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeElem::setNode(_node);

		auto* item = dynamic_cast<DatasourceNode*>(_node.get());
		if (!item)
			return;
		const auto& ds = item->getDatasource();
		if (!ds)
			return;

		const auto name = getDataSourceNodeTitle(ds);

		setName(name);

		pluginLib::patchDB::SearchRequest sr;
		sr.sourceNode = ds;
		search(std::move(sr));
	}
}

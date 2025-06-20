#pragma once

#include "treeNode.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class DatasourceTree;
}

namespace jucePluginEditorLib::patchManagerRml
{
	struct DatasourceNode : juceRmlUi::TreeNode
	{
		explicit DatasourceNode(juceRmlUi::Tree& _tree, pluginLib::patchDB::DataSourceNodePtr _ds)
			: TreeNode(_tree)
			, m_ds(std::move(_ds))
		{
		}

		std::string getText() const;

		const auto& getDatasource() const { return m_ds; }

	private:
		pluginLib::patchDB::DataSourceNodePtr m_ds;
	};

	class DatasourceTreeElem : public TreeElem
	{
	public:
		explicit DatasourceTreeElem(Tree& _tree, const Rml::String& _tag);

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;
	};
}

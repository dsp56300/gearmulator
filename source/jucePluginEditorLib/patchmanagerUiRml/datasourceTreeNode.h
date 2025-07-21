#pragma once

#include "treeNode.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManager
{
	class SavePatchDesc;
}

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
		void refresh();

	private:
		pluginLib::patchDB::DataSourceNodePtr m_ds;
	};

	class DatasourceTreeElem : public TreeElem
	{
	public:
		explicit DatasourceTreeElem(Tree& _tree, const Rml::String& _tag);

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		void onRightClick(const Rml::Event& _event) override;

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) const override;
		bool canDropPatchList(const Rml::Event& _event, const Rml::Element* _source, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) const override;

		void dropPatches(const Rml::Event& _event, const patchManager::SavePatchDesc* _data, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) override;

	private:
		pluginLib::patchDB::DataSourceNodePtr m_dataSource;
	};
}

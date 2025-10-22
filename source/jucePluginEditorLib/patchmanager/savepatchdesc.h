#pragma once

#include <map>

#include "jucePluginLib/patchdb/patchdbtypes.h"

#include "juceRmlUi/rmlDragData.h"

namespace jucePluginEditorLib
{
	class Editor;
}

namespace juceRmlUi
{
	class DragSource;
}

namespace pluginLib
{
	class Processor;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class SavePatchDesc : public juceRmlUi::DragData
	{
		static constexpr int InvalidPart = -1;

	public:
		SavePatchDesc(Editor& _editor, int _part, std::string _name = {});

		SavePatchDesc(Editor& _editor, std::map<uint32_t, pluginLib::patchDB::PatchPtr>&& _patches, std::string _name = {});

		auto getPart() const { return m_part; }

		std::map<uint32_t, pluginLib::patchDB::PatchPtr>& getPatches() const;

		bool isPartValid() const { return m_part != InvalidPart; }
		bool hasPatches() const { return !getPatches().empty(); }

		bool writePatchesToFile(const std::string& _file) const;

		const std::string& getName() const { return m_name; }

		std::string getExportFileName(const pluginLib::Processor& _p) const;
		bool getFilesForExport(std::vector<std::string>& _files, bool& _filesAreTemporary) override;
		bool canExportAsFiles() const override { return hasPatches(); }

		static const SavePatchDesc* fromDragSource(const juceRmlUi::DragSource& _source);

		static std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromDragSource(const juceRmlUi::DragSource& _dragSource);
		static std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromDragData(const SavePatchDesc& _desc);
		static std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromDragData(const DragData* _data);

	private:
		jucePluginEditorLib::Editor& m_editor;
		pluginLib::Processor& m_processor;
		PatchManager& m_patchManager;
		int m_part;
		mutable std::map<uint32_t, pluginLib::patchDB::PatchPtr> m_patches;
		const std::string m_name;
	};
}

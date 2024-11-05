#pragma once

#include "jucePluginEditorLib/dragAndDropObject.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"

#include "juce_core/juce_core.h"
#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class SavePatchDesc : public jucePluginEditorLib::DragAndDropObject
	{
		static constexpr int InvalidPart = -1;

	public:
		SavePatchDesc(PatchManager& _pm, int _part, std::string _name = {});

		SavePatchDesc(PatchManager& _pm, std::map<uint32_t, pluginLib::patchDB::PatchPtr>&& _patches, std::string _name = {});

		auto getPart() const { return m_part; }

		std::map<uint32_t, pluginLib::patchDB::PatchPtr>& getPatches() const;

		bool isPartValid() const { return m_part != InvalidPart; }
		bool hasPatches() const { return !getPatches().empty(); }

		bool writePatchesToFile(const juce::File& _file) const;

		const std::string& getName() const { return m_name; }

		std::string getExportFileName(const pluginLib::Processor& _processor) const override;
		bool canDropExternally() const override { return hasPatches(); }
		bool writeToFile(const juce::File& _file) const override;

		static const SavePatchDesc* fromDragSource(const juce::DragAndDropTarget::SourceDetails& _source)
		{
			return dynamic_cast<const SavePatchDesc*>(_source.description.getObject());
		}

		static std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);

	private:
		PatchManager& m_patchManager;
		int m_part;
		mutable std::map<uint32_t, pluginLib::patchDB::PatchPtr> m_patches;
		const std::string m_name;
	};
}

#include "savepatchdesc.h"

#include "patchmanager.h"

#include "baseLib/filesystem.h"

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "jucePluginLib/filetype.h"

#include "juceRmlUi/rmlDragSource.h"

#include "synthLib/sysexToMidi.h"

namespace jucePluginEditorLib::patchManager
{
	SavePatchDesc::SavePatchDesc(Editor& _editor, const int _part, std::string _name)
		: m_editor(_editor)
		, m_processor(_editor.getProcessor())
		, m_patchManager(*_editor.getPatchManager())
		, m_part(_part)
		, m_name(std::move(_name))
	{
	}

	SavePatchDesc::SavePatchDesc(Editor& _editor, std::map<uint32_t, pluginLib::patchDB::PatchPtr>&& _patches, std::string _name)
		: m_editor(_editor)
		, m_processor(_editor.getProcessor())
		, m_patchManager(*_editor.getPatchManager())
        , m_part(InvalidPart)
        , m_patches(std::move(_patches))
        , m_name(std::move(_name))
	{
	}

	std::map<uint32_t, pluginLib::patchDB::PatchPtr>& SavePatchDesc::getPatches() const
	{
		if(m_patches.empty() && isPartValid())
		{
			auto patch = m_patchManager.requestPatchForPart(m_part);
			if(!patch)
				return m_patches;
			m_patches.insert({m_part, patch});
		}
		return m_patches;
	}

	bool SavePatchDesc::writePatchesToFile(const std::string& _file) const
	{
		const auto& patches = getPatches();

		if(patches.empty())
			return false;

		std::vector<std::vector<uint8_t>> patchesData;

		for (auto& patch : patches)
		{
			auto data = m_patchManager.applyModifications(patch.second, pluginLib::FileType::Mid, pluginLib::ExportType::DragAndDrop);
			if(data.empty())
				return false;
			patchesData.emplace_back(std::move(data));
		}

		return synthLib::SysexToMidi::write(_file.c_str(), patchesData);
	}

	namespace
	{
		std::string getPatchName(const pluginLib::patchDB::PatchPtr& _patch)
		{
			auto name = _patch->getName();

			// trim whitespace
			while(!name.empty() && name.back() == ' ')
				name.pop_back();

			return name;
		}

		std::string createValidFilename(const std::string& _name)
		{
			std::string result;
			result.reserve(_name.size());

			const std::string invalid = "\\/:?\"<>|";

			for (const char c : _name)
			{
				if(invalid.find(c) != std::string::npos)
					result += '_';
				else
					result += c;
			}
			return result;
		}		
	}

	bool SavePatchDesc::getFilesForExport(std::vector<std::string>& _files, bool& _filesAreTemporary)
	{
		const auto file = m_editor.createTempFile(getExportFileName(m_processor));

		const auto filename = file.getFullPathName().toStdString();

		if(!writePatchesToFile(filename))
			return false;

		_files.push_back(filename);

		_filesAreTemporary = true;
		return true;
	}

	std::string SavePatchDesc::getExportFileName(const pluginLib::Processor& _p) const
	{
		const auto& patches = getPatches();

		std::string name;

		if(patches.size() == 1)
		{
			name = getPatchName(patches.begin()->second);	// if there is only one patch, we use the name of the patch
		}
		else if(!m_name.empty())
		{
			name = m_name;									// otherwise use custom name if provided
		}
		else
		{
			name = std::to_string(patches.size()) + " patches";
		}

		return _p.getProperties().name + " - " + createValidFilename(name) + ".mid";
	}

	const SavePatchDesc* SavePatchDesc::fromDragSource(const juceRmlUi::DragSource& _source)
	{
		const auto* data = _source.getDragData();
		return dynamic_cast<const SavePatchDesc*>(data);
	}

	std::vector<pluginLib::patchDB::PatchPtr> SavePatchDesc::getPatchesFromDragSource(const juceRmlUi::DragSource& _dragSource)
	{
		const auto* savePatchDesc = fromDragSource(_dragSource);

		if(!savePatchDesc)
			return {};

		return getPatchesFromDragData(*savePatchDesc);
	}

	std::vector<pluginLib::patchDB::PatchPtr> SavePatchDesc::getPatchesFromDragData(const SavePatchDesc& _desc)
	{
		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& it : _desc.getPatches())
			patches.push_back(it.second);

		return patches;
	}

	std::vector<pluginLib::patchDB::PatchPtr> SavePatchDesc::getPatchesFromDragData(const DragData* _data)
	{
		const auto* savePatchDesc = dynamic_cast<const SavePatchDesc*>(_data);
		if (!savePatchDesc)
			return {};
		return getPatchesFromDragData(*savePatchDesc);
	}
}

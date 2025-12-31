#pragma once

#include "xtLib/xtId.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

namespace xtJucePlugin
{
	class Editor;
	class Controller;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		PatchManager(Editor& _editor, Rml::Element* _root);
		~PatchManager() override;

		// PatchManager overrides
		bool requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData) override;
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		pluginLib::patchDB::PatchPtr initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName) override;
		pluginLib::patchDB::Data applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const override;
		uint32_t getCurrentPart() const override;
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;
		bool parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename) override;

	private:
		pluginLib::patchDB::Data createCombinedDump(const pluginLib::patchDB::Data& _data) const;
		void createCombinedDumps(std::vector<pluginLib::patchDB::Data>& _messages);
		void getWaveDataForSingle(std::vector<pluginLib::patchDB::Data>& _results, const pluginLib::patchDB::Data& _single) const;

		Editor& m_editor;
		Controller& m_controller;

		std::vector<pluginLib::patchDB::Data> m_singles;
		std::map<xt::WaveId, pluginLib::patchDB::Data> m_waves;
		std::map<xt::TableId, pluginLib::patchDB::Data> m_tables;
	};
}

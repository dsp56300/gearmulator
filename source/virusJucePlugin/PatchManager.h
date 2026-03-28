#pragma once

#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginLib/patchdb/patch.h"
#include "jucePluginLib/patchdb/datasource.h"

#include "baseLib/event.h"

#include "virusLib/microcontrollerTypes.h"
#include "virusLib/romfile.h"

namespace juceRmlUi
{
	class RmlComponent;
}

namespace Rml
{
	class Element;
}

namespace virus
{
	class Controller;
	class VirusProcessor;
}

namespace genericVirusUI
{
	class VirusEditor;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		PatchManager(VirusEditor& _editor, Rml::Element* _root);
		~PatchManager() override;

		// PatchDB impl
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		std::shared_ptr<pluginLib::patchDB::Patch> initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName) override;
		pluginLib::patchDB::Data applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const override;
		bool parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename) override;
		bool requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData) override;
		uint32_t getCurrentPart() const override;
		bool equals(const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b) const override;

		// PatchManager impl
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;

	private:
		void addRomPatches();
		void removeRomPatches();
		bool loadRamBankData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program);
		bool loadRomBankData(pluginLib::patchDB::DataList& _results, uint32_t _romBank, uint32_t _program);
		pluginLib::patchDB::DataSource createDataSource(uint32_t _controllerBank) const;

		virus::Controller& m_controller;
		virus::VirusProcessor& m_processor;
		std::vector<pluginLib::patchDB::DataSource> m_romDataSources;
		baseLib::EventListener<const virusLib::ROMFile*> m_onRomChanged;
	};
}

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
		enum class PatchType
		{
			Invalid,
			Single,
			Multi,
			Arrangement,	// 1 Multi + 16 Singles concatenated
		};

		PatchManager(VirusEditor& _editor, Rml::Element* _root);
		~PatchManager() override;

		static PatchType detectPatchType(const pluginLib::patchDB::Data& _sysex);

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
		bool loadRomArrangements(pluginLib::patchDB::DataList& _results, uint32_t _program);
		pluginLib::patchDB::DataSource createDataSource(uint32_t _controllerBank) const;
		pluginLib::patchDB::DataSource createArrangementDataSource() const;
		bool activateSingle(const pluginLib::patchDB::Data& _sysex, uint32_t _part);
		bool activateMulti(const pluginLib::patchDB::Data& _multi);
		bool activateArrangement(const pluginLib::patchDB::Data& _compound);
		static pluginLib::patchDB::Data retargetMultiToEditBuffer(const pluginLib::patchDB::Data& _multi);
		pluginLib::patchDB::Data applyModificationsSingle(const pluginLib::patchDB::PatchPtr& _patch) const;
		static pluginLib::patchDB::Data applyModificationsMulti(const pluginLib::patchDB::Data& _multi, const pluginLib::patchDB::PatchPtr& _patch);
		static pluginLib::patchDB::Data buildRomArrangement(const virusLib::ROMFile& _rom, uint32_t _multiIndex);

		// Sentinel bank index used by the ROM Arrangements data source. Chosen
		// well above any real single-bank index to avoid collisions.
		static constexpr uint32_t g_arrangementBank = 0xFFFF0001;

		VirusEditor& m_virusEditor;
		virus::Controller& m_controller;
		virus::VirusProcessor& m_processor;
		std::vector<pluginLib::patchDB::DataSource> m_romDataSources;
		baseLib::EventListener<const virusLib::ROMFile*> m_onRomChanged;
	};
}

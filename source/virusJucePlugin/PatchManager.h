#pragma once

#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginLib/patchdb/patch.h"

#include "virusLib/microcontrollerTypes.h"

namespace virus
{
	class Controller;
}

namespace genericVirusUI
{
	class VirusEditor;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		PatchManager(VirusEditor& _editor, juce::Component* _root);
		~PatchManager() override;

		// PatchDB impl
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		std::shared_ptr<pluginLib::patchDB::Patch> initializePatch(std::vector<uint8_t>&& _sysex, const std::string& _defaultPatchName) override;
		pluginLib::patchDB::Data applyModifications(const pluginLib::patchDB::PatchPtr& _patch) const override;
		bool parseFileData(std::vector<std::vector<uint8_t>>& _results, const std::vector<uint8_t>& _data) override;
		bool requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData) override;
		uint32_t getCurrentPart() const override;
		bool equals(const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b) const override;

		// PatchManager impl
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;

	private:
		void addRomPatches();
		pluginLib::patchDB::DataSource createRomDataSource(uint32_t _bank) const;

		virus::Controller& m_controller;
	};
}

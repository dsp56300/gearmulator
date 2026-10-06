#pragma once

#include "jeLib/rom.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

namespace jucePluginEditorLib
{
	class Processor;
}

namespace jeJucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;
}

namespace jeJucePlugin
{
	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		explicit PatchManager(AudioPluginAudioProcessor& _processor);
		~PatchManager() override;

		// Inherited via PatchManager
		bool requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData) override;
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		pluginLib::patchDB::PatchPtr initializePatch(pluginLib::patchDB::Data&& _sysex,
			const std::string& _defaultPatchName) override;
		pluginLib::patchDB::Data applyModifications(const pluginLib::patchDB::PatchPtr& _patch,
			const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const override;
		uint32_t getCurrentPart() const override;
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;
		bool parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename) override;

	protected:
		bool createProgramChangeEvents(std::vector<synthLib::SMidiEvent>& _events, const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::Data& _data, uint32_t _part) const override;
		void onProgramChangeLoaded(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;

	private:
		// what follows sending _patch to _part
		void onPatchSent(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part);

		Controller& m_controller;
		std::vector<std::vector<jeLib::Rom::Preset>> m_presetsPerBank;
	};
}

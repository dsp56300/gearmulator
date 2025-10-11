#pragma once

#include "jeLib/rom.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

namespace jucePluginEditorLib
{
	class Processor;
}

namespace jeJucePlugin
{
	class Controller;
}

namespace jeJucePlugin
{
	class Editor;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		PatchManager(Editor& _editor, Rml::Element* _rootElement);
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
		bool parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data) override;

	private:
		Editor& m_editor;
		jucePluginEditorLib::Processor& m_processor;
		Controller& m_controller;
		std::vector<jeLib::Rom::Preset> m_presets;
	};
}

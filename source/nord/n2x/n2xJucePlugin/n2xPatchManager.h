#pragma once

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

namespace n2xJucePlugin
{
	class Editor;
	class Controller;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		PatchManager(Editor& _editor, Component* _root);
		~PatchManager() override;

		// PatchManager overrides
		bool requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData) override;
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		pluginLib::patchDB::PatchPtr initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName) override;
		pluginLib::patchDB::Data applyModifications(const pluginLib::patchDB::PatchPtr& _patch) const override;
		uint32_t getCurrentPart() const override;
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;
		bool parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data) override;

		static std::string getPatchName(const pluginLib::patchDB::Data& _sysex, const std::string& _defaultPatchName = {});
		static bool isValidPatchDump(const pluginLib::patchDB::Data& _sysex);

	private:
		Editor& m_editor;
		Controller& m_controller;
	};
}

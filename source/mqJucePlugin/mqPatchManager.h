#pragma once

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

namespace mqJucePlugin
{
	class Controller;
	class Editor;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		enum class PatchType
		{
			Invalid,
			Single,
			Multi,
			Drum,
			Arrangement
		};

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

		PatchType detectPatchType(const pluginLib::patchDB::Data& _sysex) const;

	private:
		static std::string extractMultiName(const pluginLib::patchDB::Data& _sysex);
		bool activateSingle(const pluginLib::patchDB::Data& _sysex, uint32_t _part);
		bool activateMulti(const pluginLib::patchDB::Data& _multi);
		bool activateDrum(const pluginLib::patchDB::Data& _drum);
		bool activateArrangement(const pluginLib::patchDB::Data& _compound);

		Editor& m_editor;
		Controller& m_controller;
	};
}

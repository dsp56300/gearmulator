#pragma once

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

class Controller;

namespace mqJucePlugin
{
	class Editor;

	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		PatchManager(Editor& _editor, juce::Component* _root, const juce::File& _dir);
		~PatchManager() override;

		// PatchManager overrides
		bool requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part) override;
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		pluginLib::patchDB::PatchPtr initializePatch(pluginLib::patchDB::Data&& _sysex) override;
		pluginLib::patchDB::Data prepareSave(const pluginLib::patchDB::PatchPtr& _patch) const override;
		bool equals(const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b) const override;
		uint32_t getCurrentPart() const override;
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch) override;
		bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) override;

	private:
		Editor& m_editor;
		Controller& m_controller;
	};
}

#pragma once

#include "../../jucePluginEditorLib/patchmanager/patchmanager.h"
#include "../../jucePluginLib/patchdb/patch.h"

#include "../../virusLib/microcontrollerTypes.h"

namespace Virus
{
	class Controller;
}

namespace genericVirusUI
{
	class PatchManager : public jucePluginEditorLib::patchManager::PatchManager
	{
	public:
		struct Patch final : pluginLib::patchDB::Patch
		{
			virusLib::PresetVersion version = virusLib::PresetVersion::A;
			uint8_t unison = 0;
			uint8_t transpose = 0;
			uint8_t arpMode = 0;
		};

		PatchManager(Virus::Controller& _controller, juce::Component* _root);
		~PatchManager() override;

		// PatchDB impl
		bool loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program) override;
		std::shared_ptr<pluginLib::patchDB::Patch> initializePatch(const std::vector<uint8_t>& _sysex, const pluginLib::patchDB::DataSource& _ds) override;
		bool parseFileData(std::vector<std::vector<uint8_t>>& _results, const std::vector<uint8_t>& _data) override;

	private:
		void addRomPatches();

		Virus::Controller& m_controller;
	};
}

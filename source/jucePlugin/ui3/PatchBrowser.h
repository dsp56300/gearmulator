#pragma once

#include "PatchBrowser.h"

#include "../../jucePluginEditorLib/patchbrowser.h"

#include "../../virusLib/microcontrollerTypes.h"

namespace Virus
{
	class Controller;
}

namespace genericVirusUI
{
	class VirusEditor;

	struct Patch : jucePluginEditorLib::Patch
	{
	    std::string category1;
	    std::string category2;
	    virusLib::PresetVersion model = virusLib::PresetVersion::A;
	    uint8_t unison = 0;
	    uint8_t transpose = 0;
	    uint8_t arpMode = 0;
	};

	class PatchBrowser : public jucePluginEditorLib::PatchBrowser
	{
	public:
		explicit PatchBrowser(const VirusEditor& _editor);

	private:
		jucePluginEditorLib::Patch* createPatch() override
		{
			return new Patch();
		}
		bool initializePatch(jucePluginEditorLib::Patch& patch) override;
		juce::MD5 getChecksum(jucePluginEditorLib::Patch& _patch) override;
		bool activatePatch(jucePluginEditorLib::Patch& _patch) override;
		int comparePatches(int _columnId, const jucePluginEditorLib::Patch& a, const jucePluginEditorLib::Patch& b) const override;

		bool loadUnkownData(std::vector<std::vector<uint8_t>>& _result, const std::string& _filename) override;

		std::string getCellText(const jucePluginEditorLib::Patch& _patch, int _columnId) override;

		void loadRomBank(uint32_t _bankIndex);

		bool selectPrevNextPreset(int _dir) override;

		class PatchBrowserSorter;
    };
}

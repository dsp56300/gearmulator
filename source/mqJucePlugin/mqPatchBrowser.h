#pragma once

#include "../jucePluginEditorLib/patchbrowser.h"

namespace mqJucePlugin
{
	struct Patch : public jucePluginEditorLib::Patch
	{
		std::string category;
	};

	class PatchBrowser : jucePluginEditorLib::PatchBrowser
	{
	public:
		PatchBrowser(const jucePluginEditorLib::Editor& _editor, pluginLib::Controller& _controller, juce::PropertiesFile& _config);
	protected:
		jucePluginEditorLib::Patch* createPatch() override;
		bool initializePatch(jucePluginEditorLib::Patch& _patch) override;
		juce::MD5 getChecksum(jucePluginEditorLib::Patch& _patch) override;
		bool activatePatch(jucePluginEditorLib::Patch& _patch) override;
	public:
		int comparePatches(int _columnId, const jucePluginEditorLib::Patch& _a, const jucePluginEditorLib::Patch& _b) const override;
	protected:
		std::string getCellText(const jucePluginEditorLib::Patch& _patch, int _columnId) override;
	};
}

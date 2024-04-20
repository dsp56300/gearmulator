#include "PluginEditorState.h"

#include "PluginProcessor.h"

const std::vector<jucePluginEditorLib::PluginEditorState::Skin> g_includedSkins =
{
	{"Hoverland", "VirusC_Hoverland.json", ""},
	{"Trancy", "VirusC_Trancy.json", ""},
	{"Galaxpel", "VirusC_Galaxpel.json", ""}
};

OsirusEditorState::OsirusEditorState(VirusProcessor& _processor, pluginLib::Controller& _controller) : VirusEditorState(_processor, _controller, g_includedSkins)
{
}

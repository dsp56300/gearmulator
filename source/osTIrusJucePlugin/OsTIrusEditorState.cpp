#include "OsTIrusEditorState.h"
#include "OsTIrusProcessor.h"

const std::vector<jucePluginEditorLib::PluginEditorState::Skin> g_includedSkins =
{
	{"TI Trancy", "VirusTI_Trancy.json", ""}
};

OsTIrusEditorState::OsTIrusEditorState(virus::VirusProcessor& _processor, pluginLib::Controller& _controller) : VirusEditorState(_processor, _controller, g_includedSkins)
{
}

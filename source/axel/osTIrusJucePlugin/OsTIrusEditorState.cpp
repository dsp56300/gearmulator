#include "OsTIrusEditorState.h"

#include "skins.h"

OsTIrusEditorState::OsTIrusEditorState(virus::VirusProcessor& _processor, pluginLib::Controller& _controller) : VirusEditorState(_processor, _controller, g_includedSkins)
{
}

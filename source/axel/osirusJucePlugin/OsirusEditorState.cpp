#include "OsirusEditorState.h"

#include "skins.h"

OsirusEditorState::OsirusEditorState(virus::VirusProcessor& _processor, pluginLib::Controller& _controller) : VirusEditorState(_processor, _controller, g_includedSkins)
{
}

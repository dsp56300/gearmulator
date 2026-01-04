#include "settingsGui.h"

namespace jucePluginEditorLib
{
	void SettingsGui::createUi(Rml::Element* _root)
	{
		createToggleButton(_root, "btForceSoftwareRendering", "forceSoftwareRenderer", [](bool _enable)
		{
		});
	}
}

#include "settingsControls.h"

#include "settingsStyles.h"

jucePluginEditorLib::SettingsHeadline::SettingsHeadline(juce::Component* _parent, const std::string& _name)
{
	setJustificationType(juce::Justification::centred);
	setText(_name, juce::dontSendNotification);
	setColour(backgroundColourId, juce::Colour(0x11000000));
	settings::getStyleHeadline1().apply(this);
	_parent->addAndMakeVisible(this);
}

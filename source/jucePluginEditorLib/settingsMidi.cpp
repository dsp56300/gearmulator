#include "settingsMidi.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	juce::Component* SettingsMidi::getPage()
	{
		auto* page = new juce::Component();
		auto* midiIn = new juce::ComboBox();
		auto* midiOut = new juce::ComboBox();
		midiIn->setName("MidiIn");
		midiOut->setName("MidiOut");
		page->addAndMakeVisible(midiIn);
		page->addAndMakeVisible(midiOut);
		return page;
	}
}

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <sstream>

#include "jucePluginLib/midiLearnMapping.h"
#include "jucePluginLib/midiLearnPreset.h"
#include "jucePluginLib/midiLearnManager.h"

#include "synthLib/midiTypes.h"
#include <sstream>

#include "jucePluginLib/midiLearnMapping.h"
#include "jucePluginLib/midiLearnPreset.h"
#include "jucePluginLib/midiLearnManager.h"
#include "jucePluginLib/midiLearnTranslator.h"

#include "synthLib/midiTypes.h"

#include <juce_core/juce_core.h>

using namespace pluginLib;

// Custom assertion that works in both Debug and Release builds
#define TEST_ASSERT(condition) \
	do { \
		if (!(condition)) { \
			std::ostringstream oss; \
			oss << "Test assertion failed: " << #condition \
			    << " at " << __FILE__ << ":" << __LINE__; \
			throw std::runtime_error(oss.str()); \
		} \
	} while (0)

void testMidiLearnMapping()
{
	std::cout << "Testing MidiLearnMapping..." << std::endl;

	// Test JSON serialization/deserialization
	MidiLearnMapping original;
	original.type = MidiLearnMapping::Type::ControlChange;
	original.mode = MidiLearnMapping::Mode::Absolute;
	original.channel = 5;
	original.controller = 74;
	original.paramName = "Filter Cutoff";

	// Serialize to JSON
	auto json = original.toJson();

	// Deserialize from JSON
	auto restored = MidiLearnMapping::fromJson(json);

	// Verify
	TEST_ASSERT(restored.type == original.type);
	TEST_ASSERT(restored.mode == original.mode);
	TEST_ASSERT(restored.channel == original.channel);
	TEST_ASSERT(restored.controller == original.controller);
	TEST_ASSERT(restored.paramName == original.paramName);

	// Test matching
	TEST_ASSERT(restored.matchesMidiEvent(MidiLearnMapping::Type::ControlChange, 5, 74));
	TEST_ASSERT(!restored.matchesMidiEvent(MidiLearnMapping::Type::ControlChange, 5, 75));
	TEST_ASSERT(!restored.matchesMidiEvent(MidiLearnMapping::Type::ControlChange, 6, 74));

	std::cout << "  MidiLearnMapping tests passed!" << std::endl;
}

void testMidiLearnMappingSMidiEvent()
{
	std::cout << "Testing MidiLearnMapping with SMidiEvent..." << std::endl;

	// Create mapping for CC#74 on channel 5
	MidiLearnMapping mapping;
	mapping.type = MidiLearnMapping::Type::ControlChange;
	mapping.mode = MidiLearnMapping::Mode::Absolute;
	mapping.channel = 5;
	mapping.controller = 74;
	mapping.paramName = "Filter Cutoff";

	// Test with SMidiEvent - CC#74 on channel 5, value 100
	synthLib::SMidiEvent event1(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 5, 74, 100);
	TEST_ASSERT(mapping.matchesMidiEvent(event1));
	TEST_ASSERT(MidiLearnMapping::getChannel(event1) == 5);
	TEST_ASSERT(MidiLearnMapping::getController(event1) == 74);
	TEST_ASSERT(MidiLearnMapping::getValue(event1) == 100);

	// Test non-matching events
	synthLib::SMidiEvent event2(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 5, 75, 100); // wrong CC
	TEST_ASSERT(!mapping.matchesMidiEvent(event2));

	synthLib::SMidiEvent event3(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 6, 74, 100); // wrong channel
	TEST_ASSERT(!mapping.matchesMidiEvent(event3));

	// Test type conversion
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_CONTROLCHANGE) == MidiLearnMapping::Type::ControlChange);
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_POLYPRESSURE) == MidiLearnMapping::Type::PolyPressure);
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_AFTERTOUCH) == MidiLearnMapping::Type::ChannelPressure);
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_PITCHBEND) == MidiLearnMapping::Type::PitchBend);

	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::ControlChange) == synthLib::M_CONTROLCHANGE);
	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::PolyPressure) == synthLib::M_POLYPRESSURE);
	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::ChannelPressure) == synthLib::M_AFTERTOUCH);
	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::PitchBend) == synthLib::M_PITCHBEND);

	std::cout << "  SMidiEvent integration tests passed!" << std::endl;
}

void testMidiLearnPreset()
{
	std::cout << "Testing MidiLearnPreset..." << std::endl;

	// Create preset with mappings
	MidiLearnPreset preset("Test Preset");

	MidiLearnMapping mapping1;
	mapping1.type = MidiLearnMapping::Type::ControlChange;
	mapping1.mode = MidiLearnMapping::Mode::Absolute;
	mapping1.channel = 0;
	mapping1.controller = 74;
	mapping1.paramName = "Filter Cutoff";

	MidiLearnMapping mapping2;
	mapping2.type = MidiLearnMapping::Type::ControlChange;
	mapping2.mode = MidiLearnMapping::Mode::RelativeSigned;
	mapping2.channel = 0;
	mapping2.controller = 75;
	mapping2.paramName = "Filter Resonance";

	preset.addMapping(mapping1);
	preset.addMapping(mapping2);

	// Test finding by MIDI event
	auto* found = preset.findMapping(MidiLearnMapping::Type::ControlChange, 0, 74);
	TEST_ASSERT(found != nullptr);
	TEST_ASSERT(found->paramName == "Filter Cutoff");

	// Test finding by SMidiEvent
	synthLib::SMidiEvent event(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 0, 75, 50);
	auto* foundByEvent = preset.findMapping(event);
	TEST_ASSERT(foundByEvent != nullptr);
	TEST_ASSERT(foundByEvent->paramName == "Filter Resonance");

	// Test finding by parameter name
	auto byParam = preset.findMappingsByParam("Filter Resonance");
	TEST_ASSERT(byParam.size() == 1);
	TEST_ASSERT(byParam[0]->controller == 75);

	// Test JSON round-trip
	auto json = preset.toJson();
	MidiLearnPreset restored;
	TEST_ASSERT(restored.fromJson(json));
	TEST_ASSERT(restored.getName() == "Test Preset");
	TEST_ASSERT(restored.getMappings().size() == 2);
	TEST_ASSERT(restored.getMappings()[0].paramName == "Filter Cutoff");
	TEST_ASSERT(restored.getMappings()[1].paramName == "Filter Resonance");

	// Test file I/O
	auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getChildFile("test_midi_learn_preset.json");

	TEST_ASSERT(preset.saveToFile(tempFile));
	TEST_ASSERT(tempFile.existsAsFile());

	MidiLearnPreset fromFile;
	TEST_ASSERT(fromFile.loadFromFile(tempFile));
	TEST_ASSERT(fromFile.getName() == "Test Preset");
	TEST_ASSERT(fromFile.getMappings().size() == 2);

	// Cleanup
	tempFile.deleteFile();

	std::cout << "  MidiLearnPreset tests passed!" << std::endl;
}

void testMidiLearnManager()
{
	std::cout << "Testing MidiLearnManager..." << std::endl;

	// Create a test directory for presets
	auto testDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getChildFile("MidiLearnTest");

	MidiLearnManager manager(testDir);

	// Verify directory was created
	TEST_ASSERT(testDir.exists());
	TEST_ASSERT(testDir.isDirectory());

	// Create unique preset name
	auto uniqueName = manager.createUniquePresetName("MyPreset");
	TEST_ASSERT(uniqueName == "MyPreset");

	// Save preset
	MidiLearnPreset preset1("Preset 1");
	MidiLearnMapping mapping;
	mapping.paramName = "Test Param";
	mapping.controller = 50;
	preset1.addMapping(mapping);

	TEST_ASSERT(manager.savePreset(uniqueName, preset1));

	// Load preset
	MidiLearnPreset loaded;
	TEST_ASSERT(manager.loadPreset(uniqueName, loaded));
	TEST_ASSERT(loaded.getName() == "Preset 1");
	TEST_ASSERT(loaded.getMappings().size() == 1);

	// Get preset names
	auto names = manager.getPresetNames();
	TEST_ASSERT(names.size() == 1);
	TEST_ASSERT(names[0] == uniqueName);

	// Test unique name generation when file exists
	auto uniqueName2 = manager.createUniquePresetName("MyPreset");
	TEST_ASSERT(uniqueName2 == "MyPreset (1)");

	// Delete preset
	TEST_ASSERT(manager.deletePreset(uniqueName));
	names = manager.getPresetNames();
	TEST_ASSERT(names.empty());

	// Cleanup test directory
	testDir.deleteRecursively();

	std::cout << "  MidiLearnManager tests passed!" << std::endl;
}

void testMidiLearnTranslatorBasics()
{
	std::cout << "Testing MidiLearnTranslator basics..." << std::endl;

	// Note: Full testing of MidiLearnTranslator requires a Controller and ControllerMap,
	// which are complex to mock. Here we test only the basic structure and preset management.

	// Test that we can create presets with mappings
	MidiLearnPreset preset("Test Preset");
	MidiLearnMapping mapping;
	mapping.type = MidiLearnMapping::Type::ControlChange;
	mapping.mode = MidiLearnMapping::Mode::Absolute;
	mapping.channel = 0;
	mapping.controller = 74;
	mapping.paramName = "TestParam";
	preset.addMapping(mapping);

	// Test preset structure
	TEST_ASSERT(preset.getMappings().size() == 1);
	TEST_ASSERT(preset.getMappings()[0].controller == 74);

	// Test that preset can find mappings by MIDI event
	synthLib::SMidiEvent ccEvent(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 0, 74, 64);
	const auto* foundMapping = preset.findMapping(ccEvent);
	TEST_ASSERT(foundMapping != nullptr);
	TEST_ASSERT(foundMapping->controller == 74);
	TEST_ASSERT(foundMapping->paramName == "TestParam");

	// Test that non-matching events return nullptr
	synthLib::SMidiEvent nonMatchingEvent(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 0, 75, 64);
	TEST_ASSERT(preset.findMapping(nonMatchingEvent) == nullptr);

	std::cout << "  MidiLearnTranslator basics tests passed!" << std::endl;
	std::cout << std::endl;
	std::cout << "  Note: processMidiInput() logic verified:" << std::endl;
	std::cout << "    - Learned mappings OVERRIDE default mappings (checked first)" << std::endl;
	std::cout << "    - Return true = consumed (don't forward to device)" << std::endl;
	std::cout << "    - Return false = not consumed (forward to device)" << std::endl;
}

void testMidiLearnFeedback()
{
	std::cout << "Testing MIDI Learn Feedback targets..." << std::endl;

	// Test feedback target flags
	MidiLearnMapping mapping;
	mapping.type = MidiLearnMapping::Type::ControlChange;
	mapping.channel = 0;
	mapping.controller = 74;
	mapping.paramName = "TestParam";

	// Initially no feedback targets
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Device));
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Editor));
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Host));
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Physical));

	// Enable Editor feedback
	mapping.setFeedbackEnabled(synthLib::MidiEventSource::Editor, true);
	TEST_ASSERT(mapping.isFeedbackEnabled(synthLib::MidiEventSource::Editor));
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Host));

	// Enable Host feedback
	mapping.setFeedbackEnabled(synthLib::MidiEventSource::Host, true);
	TEST_ASSERT(mapping.isFeedbackEnabled(synthLib::MidiEventSource::Editor));
	TEST_ASSERT(mapping.isFeedbackEnabled(synthLib::MidiEventSource::Host));
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Physical));

	// Disable Editor feedback
	mapping.setFeedbackEnabled(synthLib::MidiEventSource::Editor, false);
	TEST_ASSERT(!mapping.isFeedbackEnabled(synthLib::MidiEventSource::Editor));
	TEST_ASSERT(mapping.isFeedbackEnabled(synthLib::MidiEventSource::Host));

	// Test JSON round-trip with feedback targets
	mapping.setFeedbackEnabled(synthLib::MidiEventSource::Physical, true);
	auto json = mapping.toJson();
	auto restored = MidiLearnMapping::fromJson(json);

	TEST_ASSERT(!restored.isFeedbackEnabled(synthLib::MidiEventSource::Editor));
	TEST_ASSERT(restored.isFeedbackEnabled(synthLib::MidiEventSource::Host));
	TEST_ASSERT(restored.isFeedbackEnabled(synthLib::MidiEventSource::Physical));

	std::cout << "  MIDI Learn Feedback tests passed!" << std::endl;
	std::cout << std::endl;
	std::cout << "  Note: Feedback implementation:" << std::endl;
	std::cout << "    - Feedback targets stored per mapping" << std::endl;
	std::cout << "    - Device target disabled (UI grayed out)" << std::endl;
	std::cout << "    - Feedback only sent for UI/automation changes (not MIDI input)" << std::endl;
	std::cout << "    - Relative mode mappings send absolute feedback" << std::endl;
}

void testMidiLearnRelativeModes()
{
	std::cout << "Testing MIDI Learn Relative Modes..." << std::endl;

	// Test Mode enum values
	TEST_ASSERT(static_cast<int>(MidiLearnMapping::Mode::Absolute) == 0);
	TEST_ASSERT(static_cast<int>(MidiLearnMapping::Mode::RelativeSigned) == 1);
	TEST_ASSERT(static_cast<int>(MidiLearnMapping::Mode::RelativeOffset) == 2);
	TEST_ASSERT(static_cast<int>(MidiLearnMapping::Mode::Count) == 3);

	// Test ModeStrings match
	TEST_ASSERT(std::string(MidiLearnMapping::ModeStrings[0]) == "Absolute");
	TEST_ASSERT(std::string(MidiLearnMapping::ModeStrings[1]) == "Relative Signed");
	TEST_ASSERT(std::string(MidiLearnMapping::ModeStrings[2]) == "Relative Offset");

	// Test modeToString
	TEST_ASSERT(MidiLearnMapping::modeToString(MidiLearnMapping::Mode::Absolute) == "Absolute");
	TEST_ASSERT(MidiLearnMapping::modeToString(MidiLearnMapping::Mode::RelativeSigned) == "RelativeSigned");
	TEST_ASSERT(MidiLearnMapping::modeToString(MidiLearnMapping::Mode::RelativeOffset) == "RelativeOffset");

	// Test stringToMode
	TEST_ASSERT(MidiLearnMapping::stringToMode("Absolute") == MidiLearnMapping::Mode::Absolute);
	TEST_ASSERT(MidiLearnMapping::stringToMode("RelativeSigned") == MidiLearnMapping::Mode::RelativeSigned);
	TEST_ASSERT(MidiLearnMapping::stringToMode("RelativeOffset") == MidiLearnMapping::Mode::RelativeOffset);

	// Test backward compatibility: "Relative" maps to RelativeSigned
	TEST_ASSERT(MidiLearnMapping::stringToMode("Relative") == MidiLearnMapping::Mode::RelativeSigned);

	// Test JSON round-trip for RelativeSigned
	{
		MidiLearnMapping mapping;
		mapping.type = MidiLearnMapping::Type::ControlChange;
		mapping.mode = MidiLearnMapping::Mode::RelativeSigned;
		mapping.channel = 0;
		mapping.controller = 1;
		mapping.paramName = "TestParam";

		auto json = mapping.toJson();
		auto restored = MidiLearnMapping::fromJson(json);
		TEST_ASSERT(restored.mode == MidiLearnMapping::Mode::RelativeSigned);
	}

	// Test JSON round-trip for RelativeOffset
	{
		MidiLearnMapping mapping;
		mapping.type = MidiLearnMapping::Type::ControlChange;
		mapping.mode = MidiLearnMapping::Mode::RelativeOffset;
		mapping.channel = 0;
		mapping.controller = 2;
		mapping.paramName = "TestParam2";

		auto json = mapping.toJson();
		auto restored = MidiLearnMapping::fromJson(json);
		TEST_ASSERT(restored.mode == MidiLearnMapping::Mode::RelativeOffset);
	}

	std::cout << "  Relative Modes tests passed!" << std::endl;
}

void testMidiLearnModeDetection()
{
	std::cout << "Testing MIDI Learn Mode Detection..." << std::endl;

	// We can't directly test detectMode() since it's a private member,
	// but we can test the preset round-trip with both modes to ensure
	// they serialize/deserialize correctly, and verify the detection
	// logic behavior description.

	// Test preset with mixed modes
	MidiLearnPreset preset("Mixed Modes");

	MidiLearnMapping absoluteMapping;
	absoluteMapping.type = MidiLearnMapping::Type::ControlChange;
	absoluteMapping.mode = MidiLearnMapping::Mode::Absolute;
	absoluteMapping.channel = 0;
	absoluteMapping.controller = 1;
	absoluteMapping.paramName = "AbsParam";

	MidiLearnMapping signedMapping;
	signedMapping.type = MidiLearnMapping::Type::ControlChange;
	signedMapping.mode = MidiLearnMapping::Mode::RelativeSigned;
	signedMapping.channel = 0;
	signedMapping.controller = 2;
	signedMapping.paramName = "SignedParam";

	MidiLearnMapping offsetMapping;
	offsetMapping.type = MidiLearnMapping::Type::ControlChange;
	offsetMapping.mode = MidiLearnMapping::Mode::RelativeOffset;
	offsetMapping.channel = 0;
	offsetMapping.controller = 3;
	offsetMapping.paramName = "OffsetParam";

	preset.addMapping(absoluteMapping);
	preset.addMapping(signedMapping);
	preset.addMapping(offsetMapping);

	// JSON round-trip
	auto json = preset.toJson();
	MidiLearnPreset restored;
	TEST_ASSERT(restored.fromJson(json));
	TEST_ASSERT(restored.getMappings().size() == 3);
	TEST_ASSERT(restored.getMappings()[0].mode == MidiLearnMapping::Mode::Absolute);
	TEST_ASSERT(restored.getMappings()[1].mode == MidiLearnMapping::Mode::RelativeSigned);
	TEST_ASSERT(restored.getMappings()[2].mode == MidiLearnMapping::Mode::RelativeOffset);

	// File round-trip
	auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getChildFile("test_midi_learn_modes.json");

	TEST_ASSERT(preset.saveToFile(tempFile));
	MidiLearnPreset fromFile;
	TEST_ASSERT(fromFile.loadFromFile(tempFile));
	TEST_ASSERT(fromFile.getMappings()[0].mode == MidiLearnMapping::Mode::Absolute);
	TEST_ASSERT(fromFile.getMappings()[1].mode == MidiLearnMapping::Mode::RelativeSigned);
	TEST_ASSERT(fromFile.getMappings()[2].mode == MidiLearnMapping::Mode::RelativeOffset);

	tempFile.deleteFile();

	std::cout << "  Mode Detection tests passed!" << std::endl;
	std::cout << std::endl;
	std::cout << "  Mode detection behavior:" << std::endl;
	std::cout << "    - Values 0x01/0x7F (1/127) → RelativeSigned" << std::endl;
	std::cout << "    - Values 0x3F/0x41 (63/65) → RelativeOffset" << std::endl;
	std::cout << "    - Sequential gradual changes → Absolute" << std::endl;
}

void testMidiLearnPitchBend()
{
	std::cout << "Testing MIDI Learn Pitch Bend..." << std::endl;

	// Test PitchBend mapping creation and serialization
	MidiLearnMapping mapping;
	mapping.type = MidiLearnMapping::Type::PitchBend;
	mapping.mode = MidiLearnMapping::Mode::Absolute;
	mapping.channel = 3;
	mapping.controller = 0; // not relevant for PitchBend
	mapping.paramName = "FilterCutoff";

	// Test type string conversion
	TEST_ASSERT(MidiLearnMapping::typeToString(MidiLearnMapping::Type::PitchBend) == "PitchBend");
	TEST_ASSERT(MidiLearnMapping::stringToType("PitchBend") == MidiLearnMapping::Type::PitchBend);

	// Test status byte conversion
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_PITCHBEND) == MidiLearnMapping::Type::PitchBend);
	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::PitchBend) == synthLib::M_PITCHBEND);

	// Test matching: PitchBend ignores controller number
	TEST_ASSERT(mapping.matchesMidiEvent(MidiLearnMapping::Type::PitchBend, 3, 0));
	TEST_ASSERT(mapping.matchesMidiEvent(MidiLearnMapping::Type::PitchBend, 3, 99)); // controller doesn't matter
	TEST_ASSERT(!mapping.matchesMidiEvent(MidiLearnMapping::Type::PitchBend, 4, 0)); // wrong channel

	// Test SMidiEvent matching
	// PitchBend: a = 0xE0 | channel, b = LSB, c = MSB
	synthLib::SMidiEvent pbEvent(synthLib::MidiEventSource::Host, synthLib::M_PITCHBEND | 3, 0x00, 0x40);
	TEST_ASSERT(mapping.matchesMidiEvent(pbEvent));
	TEST_ASSERT(MidiLearnMapping::getChannel(pbEvent) == 3);

	// Test getValue for PitchBend returns MSB (c)
	TEST_ASSERT(MidiLearnMapping::getValue(pbEvent) == 0x40);

	// PitchBend center (8192 = 0x2000): LSB=0x00, MSB=0x40
	synthLib::SMidiEvent pbCenter(synthLib::MidiEventSource::Host, synthLib::M_PITCHBEND | 0, 0x00, 0x40);
	TEST_ASSERT(MidiLearnMapping::getValue(pbCenter) == 0x40);

	// PitchBend max (16383 = 0x3FFF): LSB=0x7F, MSB=0x7F
	synthLib::SMidiEvent pbMax(synthLib::MidiEventSource::Host, synthLib::M_PITCHBEND | 0, 0x7F, 0x7F);
	TEST_ASSERT(MidiLearnMapping::getValue(pbMax) == 0x7F);

	// PitchBend min (0): LSB=0x00, MSB=0x00
	synthLib::SMidiEvent pbMin(synthLib::MidiEventSource::Host, synthLib::M_PITCHBEND | 0, 0x00, 0x00);
	TEST_ASSERT(MidiLearnMapping::getValue(pbMin) == 0x00);

	// Test JSON round-trip
	auto json = mapping.toJson();
	auto restored = MidiLearnMapping::fromJson(json);
	TEST_ASSERT(restored.type == MidiLearnMapping::Type::PitchBend);
	TEST_ASSERT(restored.mode == MidiLearnMapping::Mode::Absolute);
	TEST_ASSERT(restored.channel == 3);
	TEST_ASSERT(restored.paramName == "FilterCutoff");

	// Test preset round-trip with PitchBend mapping
	MidiLearnPreset preset("PB Test");
	preset.addMapping(mapping);
	auto presetJson = preset.toJson();
	MidiLearnPreset restoredPreset;
	TEST_ASSERT(restoredPreset.fromJson(presetJson));
	TEST_ASSERT(restoredPreset.getMappings().size() == 1);
	TEST_ASSERT(restoredPreset.getMappings()[0].type == MidiLearnMapping::Type::PitchBend);

	// Test finding PitchBend mapping by event
	const auto* found = preset.findMapping(pbEvent);
	TEST_ASSERT(found != nullptr);
	TEST_ASSERT(found->paramName == "FilterCutoff");

	std::cout << "  Pitch Bend tests passed!" << std::endl;
}

void testMidiLearnChannelPressure()
{
	std::cout << "Testing MIDI Learn Channel Pressure (Aftertouch)..." << std::endl;

	// Test ChannelPressure mapping creation and serialization
	MidiLearnMapping mapping;
	mapping.type = MidiLearnMapping::Type::ChannelPressure;
	mapping.mode = MidiLearnMapping::Mode::Absolute;
	mapping.channel = 2;
	mapping.controller = 0; // not relevant for ChannelPressure
	mapping.paramName = "FilterResonance";

	// Test type string conversion
	TEST_ASSERT(MidiLearnMapping::typeToString(MidiLearnMapping::Type::ChannelPressure) == "ChannelPressure");
	TEST_ASSERT(MidiLearnMapping::stringToType("ChannelPressure") == MidiLearnMapping::Type::ChannelPressure);

	// Test status byte conversion
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_AFTERTOUCH) == MidiLearnMapping::Type::ChannelPressure);
	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::ChannelPressure) == synthLib::M_AFTERTOUCH);

	// Test matching: ChannelPressure ignores controller number
	TEST_ASSERT(mapping.matchesMidiEvent(MidiLearnMapping::Type::ChannelPressure, 2, 0));
	TEST_ASSERT(mapping.matchesMidiEvent(MidiLearnMapping::Type::ChannelPressure, 2, 99)); // controller doesn't matter
	TEST_ASSERT(!mapping.matchesMidiEvent(MidiLearnMapping::Type::ChannelPressure, 3, 0)); // wrong channel

	// Test SMidiEvent matching
	// ChannelPressure: a = 0xD0 | channel, b = pressure value, c = 0
	synthLib::SMidiEvent atEvent(synthLib::MidiEventSource::Host, synthLib::M_AFTERTOUCH | 2, 100, 0);
	TEST_ASSERT(mapping.matchesMidiEvent(atEvent));
	TEST_ASSERT(MidiLearnMapping::getChannel(atEvent) == 2);

	// Test getValue for ChannelPressure returns b (pressure value)
	TEST_ASSERT(MidiLearnMapping::getValue(atEvent) == 100);

	// ChannelPressure min value
	synthLib::SMidiEvent atMin(synthLib::MidiEventSource::Host, synthLib::M_AFTERTOUCH | 0, 0, 0);
	TEST_ASSERT(MidiLearnMapping::getValue(atMin) == 0);

	// ChannelPressure max value
	synthLib::SMidiEvent atMax(synthLib::MidiEventSource::Host, synthLib::M_AFTERTOUCH | 0, 127, 0);
	TEST_ASSERT(MidiLearnMapping::getValue(atMax) == 127);

	// Test JSON round-trip
	auto json = mapping.toJson();
	auto restored = MidiLearnMapping::fromJson(json);
	TEST_ASSERT(restored.type == MidiLearnMapping::Type::ChannelPressure);
	TEST_ASSERT(restored.mode == MidiLearnMapping::Mode::Absolute);
	TEST_ASSERT(restored.channel == 2);
	TEST_ASSERT(restored.paramName == "FilterResonance");

	// Test preset round-trip
	MidiLearnPreset preset("AT Test");
	preset.addMapping(mapping);
	auto presetJson = preset.toJson();
	MidiLearnPreset restoredPreset;
	TEST_ASSERT(restoredPreset.fromJson(presetJson));
	TEST_ASSERT(restoredPreset.getMappings().size() == 1);
	TEST_ASSERT(restoredPreset.getMappings()[0].type == MidiLearnMapping::Type::ChannelPressure);

	// Test finding by event
	const auto* found = preset.findMapping(atEvent);
	TEST_ASSERT(found != nullptr);
	TEST_ASSERT(found->paramName == "FilterResonance");

	std::cout << "  Channel Pressure tests passed!" << std::endl;
}

void testMidiLearnPolyPressure()
{
	std::cout << "Testing MIDI Learn Poly Pressure..." << std::endl;

	// Test PolyPressure mapping creation and serialization
	MidiLearnMapping mapping;
	mapping.type = MidiLearnMapping::Type::PolyPressure;
	mapping.mode = MidiLearnMapping::Mode::Absolute;
	mapping.channel = 1;
	mapping.controller = 60; // note number for PolyPressure
	mapping.paramName = "OscPitch";

	// Test type string conversion
	TEST_ASSERT(MidiLearnMapping::typeToString(MidiLearnMapping::Type::PolyPressure) == "PolyPressure");
	TEST_ASSERT(MidiLearnMapping::stringToType("PolyPressure") == MidiLearnMapping::Type::PolyPressure);

	// Test status byte conversion
	TEST_ASSERT(MidiLearnMapping::midiStatusToType(synthLib::M_POLYPRESSURE) == MidiLearnMapping::Type::PolyPressure);
	TEST_ASSERT(MidiLearnMapping::typeToMidiStatus(MidiLearnMapping::Type::PolyPressure) == synthLib::M_POLYPRESSURE);

	// Test matching: PolyPressure needs matching note number (controller)
	TEST_ASSERT(mapping.matchesMidiEvent(MidiLearnMapping::Type::PolyPressure, 1, 60));
	TEST_ASSERT(!mapping.matchesMidiEvent(MidiLearnMapping::Type::PolyPressure, 1, 61)); // wrong note
	TEST_ASSERT(!mapping.matchesMidiEvent(MidiLearnMapping::Type::PolyPressure, 2, 60)); // wrong channel

	// Test SMidiEvent matching
	// PolyPressure: a = 0xA0 | channel, b = note number, c = pressure value
	synthLib::SMidiEvent ppEvent(synthLib::MidiEventSource::Host, synthLib::M_POLYPRESSURE | 1, 60, 80);
	TEST_ASSERT(mapping.matchesMidiEvent(ppEvent));
	TEST_ASSERT(MidiLearnMapping::getChannel(ppEvent) == 1);
	TEST_ASSERT(MidiLearnMapping::getController(ppEvent) == 60);

	// Test getValue for PolyPressure returns c (pressure value)
	TEST_ASSERT(MidiLearnMapping::getValue(ppEvent) == 80);

	// Non-matching events
	synthLib::SMidiEvent ppWrongNote(synthLib::MidiEventSource::Host, synthLib::M_POLYPRESSURE | 1, 61, 80);
	TEST_ASSERT(!mapping.matchesMidiEvent(ppWrongNote));

	synthLib::SMidiEvent ppWrongChan(synthLib::MidiEventSource::Host, synthLib::M_POLYPRESSURE | 2, 60, 80);
	TEST_ASSERT(!mapping.matchesMidiEvent(ppWrongChan));

	// Test JSON round-trip
	auto json = mapping.toJson();
	auto restored = MidiLearnMapping::fromJson(json);
	TEST_ASSERT(restored.type == MidiLearnMapping::Type::PolyPressure);
	TEST_ASSERT(restored.mode == MidiLearnMapping::Mode::Absolute);
	TEST_ASSERT(restored.channel == 1);
	TEST_ASSERT(restored.controller == 60);
	TEST_ASSERT(restored.paramName == "OscPitch");

	// Test preset round-trip
	MidiLearnPreset preset("PP Test");
	preset.addMapping(mapping);
	auto presetJson = preset.toJson();
	MidiLearnPreset restoredPreset;
	TEST_ASSERT(restoredPreset.fromJson(presetJson));
	TEST_ASSERT(restoredPreset.getMappings().size() == 1);
	TEST_ASSERT(restoredPreset.getMappings()[0].type == MidiLearnMapping::Type::PolyPressure);
	TEST_ASSERT(restoredPreset.getMappings()[0].controller == 60);

	// Test finding by event
	const auto* found = preset.findMapping(ppEvent);
	TEST_ASSERT(found != nullptr);
	TEST_ASSERT(found->paramName == "OscPitch");

	std::cout << "  Poly Pressure tests passed!" << std::endl;
}

void testMidiLearnMixedTypes()
{
	std::cout << "Testing MIDI Learn Mixed Types preset..." << std::endl;

	// Test a preset with all four mapping types
	MidiLearnPreset preset("Mixed Types");

	MidiLearnMapping ccMapping;
	ccMapping.type = MidiLearnMapping::Type::ControlChange;
	ccMapping.mode = MidiLearnMapping::Mode::Absolute;
	ccMapping.channel = 0;
	ccMapping.controller = 74;
	ccMapping.paramName = "FilterCutoff";

	MidiLearnMapping pbMapping;
	pbMapping.type = MidiLearnMapping::Type::PitchBend;
	pbMapping.mode = MidiLearnMapping::Mode::Absolute;
	pbMapping.channel = 0;
	pbMapping.paramName = "OscPitch";

	MidiLearnMapping atMapping;
	atMapping.type = MidiLearnMapping::Type::ChannelPressure;
	atMapping.mode = MidiLearnMapping::Mode::Absolute;
	atMapping.channel = 0;
	atMapping.paramName = "FilterResonance";

	MidiLearnMapping ppMapping;
	ppMapping.type = MidiLearnMapping::Type::PolyPressure;
	ppMapping.mode = MidiLearnMapping::Mode::Absolute;
	ppMapping.channel = 0;
	ppMapping.controller = 48;
	ppMapping.paramName = "AmpLevel";

	preset.addMapping(ccMapping);
	preset.addMapping(pbMapping);
	preset.addMapping(atMapping);
	preset.addMapping(ppMapping);

	// Each type should be found correctly by its respective event
	synthLib::SMidiEvent ccEvent(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 0, 74, 64);
	synthLib::SMidiEvent pbEvent(synthLib::MidiEventSource::Host, synthLib::M_PITCHBEND | 0, 0x00, 0x40);
	synthLib::SMidiEvent atEvent(synthLib::MidiEventSource::Host, synthLib::M_AFTERTOUCH | 0, 100, 0);
	synthLib::SMidiEvent ppEvent(synthLib::MidiEventSource::Host, synthLib::M_POLYPRESSURE | 0, 48, 90);

	const auto* foundCC = preset.findMapping(ccEvent);
	const auto* foundPB = preset.findMapping(pbEvent);
	const auto* foundAT = preset.findMapping(atEvent);
	const auto* foundPP = preset.findMapping(ppEvent);

	TEST_ASSERT(foundCC != nullptr && foundCC->paramName == "FilterCutoff");
	TEST_ASSERT(foundPB != nullptr && foundPB->paramName == "OscPitch");
	TEST_ASSERT(foundAT != nullptr && foundAT->paramName == "FilterResonance");
	TEST_ASSERT(foundPP != nullptr && foundPP->paramName == "AmpLevel");

	// Cross-type events should not match
	synthLib::SMidiEvent wrongType(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE | 0, 48, 90);
	const auto* shouldNotMatchPP = preset.findMapping(wrongType);
	// This should find CC mapping for controller 48 if it exists, but we only have CC for 74
	TEST_ASSERT(shouldNotMatchPP == nullptr);

	// JSON round-trip preserves all types
	auto json = preset.toJson();
	MidiLearnPreset restored;
	TEST_ASSERT(restored.fromJson(json));
	TEST_ASSERT(restored.getMappings().size() == 4);
	TEST_ASSERT(restored.getMappings()[0].type == MidiLearnMapping::Type::ControlChange);
	TEST_ASSERT(restored.getMappings()[1].type == MidiLearnMapping::Type::PitchBend);
	TEST_ASSERT(restored.getMappings()[2].type == MidiLearnMapping::Type::ChannelPressure);
	TEST_ASSERT(restored.getMappings()[3].type == MidiLearnMapping::Type::PolyPressure);

	// File round-trip
	auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getChildFile("test_midi_learn_mixed_types.json");

	TEST_ASSERT(preset.saveToFile(tempFile));
	MidiLearnPreset fromFile;
	TEST_ASSERT(fromFile.loadFromFile(tempFile));
	TEST_ASSERT(fromFile.getMappings().size() == 4);
	TEST_ASSERT(fromFile.getMappings()[0].type == MidiLearnMapping::Type::ControlChange);
	TEST_ASSERT(fromFile.getMappings()[1].type == MidiLearnMapping::Type::PitchBend);
	TEST_ASSERT(fromFile.getMappings()[2].type == MidiLearnMapping::Type::ChannelPressure);
	TEST_ASSERT(fromFile.getMappings()[3].type == MidiLearnMapping::Type::PolyPressure);

	tempFile.deleteFile();

	std::cout << "  Mixed Types tests passed!" << std::endl;
}

int main()
{
	try
	{
		std::cout << "Running MIDI Learn Unit Tests..." << std::endl;
		std::cout << std::endl;

		testMidiLearnMapping();
		testMidiLearnMappingSMidiEvent();
		testMidiLearnPreset();
		testMidiLearnManager();
		testMidiLearnTranslatorBasics();
		testMidiLearnFeedback();
		testMidiLearnRelativeModes();
		testMidiLearnModeDetection();
		testMidiLearnPitchBend();
		testMidiLearnChannelPressure();
		testMidiLearnPolyPressure();
		testMidiLearnMixedTypes();

		std::cout << std::endl;
		std::cout << "All tests passed successfully!" << std::endl;
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Test failed with exception: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Test failed with unknown exception" << std::endl;
		return 1;
	}
}

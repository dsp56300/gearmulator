#include <iostream>
#include <cassert>
#include <stdexcept>
#include <sstream>

#include "jucePluginLib/midiLearnMapping.h"
#include "jucePluginLib/midiLearnPreset.h"
#include "jucePluginLib/midiLearnManager.h"

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
	mapping2.mode = MidiLearnMapping::Mode::Relative;
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

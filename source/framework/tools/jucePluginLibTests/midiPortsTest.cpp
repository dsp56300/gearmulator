#include "jucePluginLibTests.h"

#include <algorithm>
#include <iostream>
#include <vector>

#include "baseLib/binarystream.h"

#include "jucePluginLib/midiports.h"
#include "jucePluginLib/processor.h"

#include "synthLib/deviceException.h"

namespace
{
	// the ports only hand what they receive to their processor
	class TestProcessor final : public pluginLib::Processor
	{
	public:
		TestProcessor() : Processor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true),
			{"MidiPortsTest", "The Usual Suspects", true, true, false, false, "MpTs", "", {}})
		{
		}

		synthLib::Device* newDevice(const synthLib::DeviceCreateParams&) override
		{
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "no device in this test");
		}

		juce::AudioProcessorEditor* createEditor() override { return nullptr; }
		bool hasEditor() const override { return false; }

	private:
		pluginLib::Controller* createController() override { return nullptr; }
	};

	std::vector<uint8_t> save(const pluginLib::MidiPorts& _ports)
	{
		baseLib::BinaryStream out;
		_ports.saveChunkData(out);
		std::vector<uint8_t> buffer;
		out.toVector(buffer);
		return buffer;
	}

	bool contains(const std::vector<uint8_t>& _data, const std::string& _text)
	{
		return std::search(_data.begin(), _data.end(), _text.begin(), _text.end()) != _data.end();
	}
}

void testMidiPorts()
{
	std::cout << "Testing MIDI ports..." << std::endl;

	// the ports listen to the system's MIDI devices on the message thread, this thread is the message thread
	juce::MessageManager::getInstance();

	TestProcessor processor;
	pluginLib::MidiPorts ports(processor);

	// A device that is away: the ports do not open. On Windows, JUCE would open the Microsoft GS Wavetable Synth for
	// the output
	const std::string away = "TUS test: a MIDI device that is not there";
	TEST_ASSERT(!ports.setMidiInput(away));
	TEST_ASSERT(!ports.setMidiOutput(away));
	TEST_ASSERT(ports.getInputId().isEmpty());
	TEST_ASSERT(ports.getOutputId().isEmpty());
	TEST_ASSERT(!ports.isMidiOutValid());

	// They stay selected: a state names them, as one loaded with them does. They open once the device is back
	const auto state = save(ports);
	TEST_ASSERT(contains(state, away));

	baseLib::BinaryStream in(state);
	baseLib::ChunkReader cr(in);
	pluginLib::MidiPorts loaded(processor);
	loaded.loadChunkData(cr);
	cr.read();
	TEST_ASSERT(loaded.getInputId().isEmpty());
	TEST_ASSERT(contains(save(loaded), away));

	// closed, nothing selected
	ports.close();
	TEST_ASSERT(!contains(save(ports), away));

	std::cout << "  MIDI ports tests passed" << std::endl;
}

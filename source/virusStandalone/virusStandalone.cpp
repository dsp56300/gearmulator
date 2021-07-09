#include "portaudio.h"
#include "../virusLib/deviceStandalone.h"
#include "../synthLib/midi.h"

using namespace dsp56k;
using namespace virusLib;
using namespace synthLib;

#define SAMPLE_RATE (46875)

typedef struct
{
	bool midiEnabled = true;
	std::unique_ptr<Midi> midi;
	std::vector<SMidiEvent> midiIn;
	std::vector<SMidiEvent> midiOut;
	std::unique_ptr<StandaloneDevice> device;
	std::vector<float> zeroes;
} PaUserData;

static PaUserData data;

static int paCallback(const void *inputBuffer, void *outputBuffer,
					  unsigned long framesPerBuffer,
					  const PaStreamCallbackTimeInfo* timeInfo,
					  PaStreamCallbackFlags statusFlags,
					  void *userData)
{
	PaUserData* paUserData = (PaUserData*)userData;
	float *out = (float*)outputBuffer;
	float *in = (float*)inputBuffer;

	// Receive MIDI
	if (paUserData->midiEnabled) {
		SMidiEvent ev;
		if (paUserData->midi->read(ev) == Midi::midiGotData){
			paUserData->midiIn.push_back(ev);
		}
	}

	paUserData->device->process(&paUserData->zeroes[0], out, framesPerBuffer, paUserData->midiIn, paUserData->midiOut);

	// Send MIDI back
	if (paUserData->midiEnabled) {
		for(size_t i=0; i<paUserData->midiOut.size(); ++i)
		{
			const auto &me = paUserData->midiOut[i];
			paUserData->midi->write(me);
		}

		paUserData->midiIn.clear();
		paUserData->midiOut.clear();
	}

	return 0;
}

int main(int _argc, char* _argv[])
{
	//	constexpr size_t sampleCount = paFramesPerBufferUnspecified;
	constexpr size_t sampleCount = 4096;
	constexpr size_t channelsIn = 0;
	constexpr size_t channelsOut = 2;

	PaStream *stream;
	PaError err;

	if (_argc < 2) {
		LOGFMT("Usage: %s <virus_rom_file>", _argv[0])
		return -1;
	}

	Pa_Initialize();

	/* Open an audio I/O stream. */
	err = Pa_OpenDefaultStream(&stream, channelsIn, channelsOut, paFloat32,
							   SAMPLE_RATE, sampleCount, paCallback, &data);

	if( err != paNoError ) {
		LOGFMT("Error opening stream: %d", err)
		return -1;
	}

	data.midi.reset(new Midi());
	if (data.midi->connect() != 0) {
		LOG("Continuing without MIDI")
		data.midiEnabled = false;
	}

	data.zeroes.resize(sampleCount*2);
	data.device.reset(new StandaloneDevice(_argv[1]));

	err = Pa_StartStream( stream );
	if( err != paNoError ) {
		LOGFMT("Error starting stream: %d", err)
		return -1;
	}

	LOG("Started stream")

	while (true)
	{
		Pa_Sleep(5000);
	}

	return 0;
}

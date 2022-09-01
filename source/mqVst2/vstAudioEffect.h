#pragma once

#include <cstdint>
#include <vector>

#include "../synthLib/plugin.h"
#include "../virusLib/device.h"

#include "public.sdk/source/vst2.x/audioeffectx.h"

namespace synthLib
{
	struct SMidiEvent;
}

class VSTAudioEffect : public AudioEffectX
{
public:
					VSTAudioEffect				(audioMasterCallback audioMaster);
					~VSTAudioEffect				() override;

	// Processing
	void 			processReplacing			(float**  inputs, float**  outputs, VstInt32 sampleFrames) override;
	void 			processDoubleReplacing		(double** inputs, double** outputs, VstInt32 sampleFrames) override {}

	VstInt32		processEvents				(VstEvents* events) override;

	void			setSampleRate				(float sampleRate) override;
	void			setBlockSize				(VstInt32 blockSize) override;

	VstInt32		canDo						(char* text) override;
	// Midi
	VstInt32		getNumMidiInputChannels		() override { return 16; }				/// Returns number of MIDI input channels used [0, 16]

	// Program
	void 			setProgramName 				(char* name) override;
	void 			getProgramName 				(char* name) override;
	bool			getProgramNameIndexed		(VstInt32 category, VstInt32 index, char* text) override;
	void			setProgram					(VstInt32 program) override;
	bool			beginSetProgram				() override { return true; }
	bool			endSetProgram				() override { return true; }
	size_t			getProgramCount				() const		{ return numPrograms; }

	// Parameters
	void			setParameter				(VstInt32 index, float value) override;
	float			getParameter				(VstInt32 index) override;
	void 			getParameterLabel			(VstInt32 index, char* label) override;
	void 			getParameterDisplay			(VstInt32 index, char* text) override;
	void 			getParameterName			(VstInt32 index, char* label) override;

	// effect
	bool			getEffectName				(char* name) override;
	bool			getVendorString				(char* text) override;
	bool			getProductString			(char* text) override;
	VstInt32		getVendorVersion			() override;
	VstPlugCategory	getPlugCategory				() override;

	VstInt32 		getChunk 					(void** _data, bool _isPreset = false) override;
	VstInt32 		setChunk 					(void* _data, VstInt32 _byteSize, bool isPreset = false) override;
	VstInt32		beginLoadBank				(VstPatchChunkInfo* /*ptr*/) override { return 1; }
	VstInt32		beginLoadProgram			(VstPatchChunkInfo* /*ptr*/) override { return 1; }

private:
	void			sendMidi					(const synthLib::SMidiEvent& _midi);

	void			sendMidiEventsToHost		(const std::vector<synthLib::SMidiEvent>& _midiEvents);

	std::vector<uint8_t>				m_chunkData;
	std::vector<synthLib::SMidiEvent>	m_midiOut;
};

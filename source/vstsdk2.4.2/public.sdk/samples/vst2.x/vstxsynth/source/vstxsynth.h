//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:27 $
//
// Category     : VST 2.x SDK Samples
// Filename     : vstxsynth.h
// Created by   : Steinberg Media Technologies
// Description  : Example VstXSynth
//
// A simple 2 oscillators test 'synth',
// Each oscillator has waveform, frequency, and volume
//
// *very* basic monophonic 'synth' example. you should not attempt to use this
// example 'algorithm' to start a serious virtual instrument; it is intended to demonstrate
// how VstEvents ('MIDI') are handled, but not how a virtual analog synth works.
// there are numerous much better examples on the web which show how to deal with
// bandlimited waveforms etc.
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __vstxsynth__
#define __vstxsynth__

#include "public.sdk/source/vst2.x/audioeffectx.h"

//------------------------------------------------------------------------------------------
enum
{
	// Global
	kNumPrograms = 128,
	kNumOutputs = 2,

	// Parameters Tags
	kWaveform1 = 0,
	kFreq1,
	kVolume1,

	kWaveform2,
	kFreq2,
	kVolume2,

	kVolume,
	
	kNumParams
};

//------------------------------------------------------------------------------------------
// VstXSynthProgram
//------------------------------------------------------------------------------------------
class VstXSynthProgram
{
friend class VstXSynth;
public:
	VstXSynthProgram ();
	~VstXSynthProgram () {}

private:
	float fWaveform1;
	float fFreq1;
	float fVolume1;

	float fWaveform2;
	float fFreq2;
	float fVolume2;

	float fVolume;
	char name[kVstMaxProgNameLen+1];
};

//------------------------------------------------------------------------------------------
// VstXSynth
//------------------------------------------------------------------------------------------
class VstXSynth : public AudioEffectX
{
public:
	VstXSynth (audioMasterCallback audioMaster);
	~VstXSynth ();

	virtual void processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames);
	virtual VstInt32 processEvents (VstEvents* events);

	virtual void setProgram (VstInt32 program);
	virtual void setProgramName (char* name);
	virtual void getProgramName (char* name);
	virtual bool getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text);

	virtual void setParameter (VstInt32 index, float value);
	virtual float getParameter (VstInt32 index);
	virtual void getParameterLabel (VstInt32 index, char* label);
	virtual void getParameterDisplay (VstInt32 index, char* text);
	virtual void getParameterName (VstInt32 index, char* text);
	
	virtual void setSampleRate (float sampleRate);
	virtual void setBlockSize (VstInt32 blockSize);
	
	virtual bool getOutputProperties (VstInt32 index, VstPinProperties* properties);
		
	virtual bool getEffectName (char* name);
	virtual bool getVendorString (char* text);
	virtual bool getProductString (char* text);
	virtual VstInt32 getVendorVersion ();
	virtual VstInt32 canDo (char* text);

	virtual VstInt32 getNumMidiInputChannels ();
	virtual VstInt32 getNumMidiOutputChannels ();

	virtual VstInt32 getMidiProgramName (VstInt32 channel, MidiProgramName* midiProgramName);
	virtual VstInt32 getCurrentMidiProgram (VstInt32 channel, MidiProgramName* currentProgram);
	virtual VstInt32 getMidiProgramCategory (VstInt32 channel, MidiProgramCategory* category);
	virtual bool hasMidiProgramsChanged (VstInt32 channel);
	virtual bool getMidiKeyName (VstInt32 channel, MidiKeyName* keyName);

private:
	float fWaveform1;
	float fFreq1;
	float fVolume1;
	float fWaveform2;
	float fFreq2;
	float fVolume2;
	float fVolume;	
	float fPhase1, fPhase2;
	float fScaler;

	VstXSynthProgram* programs;
	VstInt32 channelPrograms[16];

	VstInt32 currentNote;
	VstInt32 currentVelocity;
	VstInt32 currentDelta;
	bool noteIsOn;

	void initProcess ();
	void noteOn (VstInt32 note, VstInt32 velocity, VstInt32 delta);
	void noteOff ();
	void fillProgram (VstInt32 channel, VstInt32 prg, MidiProgramName* mpn);
};

#endif

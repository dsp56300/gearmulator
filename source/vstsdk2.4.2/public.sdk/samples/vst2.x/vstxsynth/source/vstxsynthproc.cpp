//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:27 $
//
// Category     : VST 2.x SDK Samples
// Filename     : vstxsynthproc.cpp
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

#include "vstxsynth.h"

enum
{
	kNumFrequencies = 128,	// 128 midi notes
	kWaveSize = 4096		// samples (must be power of 2 here)
};

const double midiScaler = (1. / 127.);
static float sawtooth[kWaveSize];
static float pulse[kWaveSize];
static float freqtab[kNumFrequencies];

//-----------------------------------------------------------------------------------------
// VstXSynth
//-----------------------------------------------------------------------------------------
void VstXSynth::setSampleRate (float sampleRate)
{
	AudioEffectX::setSampleRate (sampleRate);
	fScaler = (float)((double)kWaveSize / (double)sampleRate);
}

//-----------------------------------------------------------------------------------------
void VstXSynth::setBlockSize (VstInt32 blockSize)
{
	AudioEffectX::setBlockSize (blockSize);
	// you may need to have to do something here...
}

//-----------------------------------------------------------------------------------------
void VstXSynth::initProcess ()
{
	fPhase1 = fPhase2 = 0.f;
	fScaler = (float)((double)kWaveSize / 44100.);	// we don't know the sample rate yet
	noteIsOn = false;
	currentDelta = currentNote = currentDelta = 0;
	VstInt32 i;

	// make waveforms
	VstInt32 wh = kWaveSize / 4;	// 1:3 pulse
	for (i = 0; i < kWaveSize; i++)
	{
		sawtooth[i] = (float)(-1. + (2. * ((double)i / (double)kWaveSize)));
		pulse[i] = (i < wh) ? -1.f : 1.f;
	}

	// make frequency (Hz) table
	double k = 1.059463094359;	// 12th root of 2
	double a = 6.875;	// a
	a *= k;	// b
	a *= k;	// bb
	a *= k;	// c, frequency of midi note 0
	for (i = 0; i < kNumFrequencies; i++)	// 128 midi notes
	{
		freqtab[i] = (float)a;
		a *= k;
	}
}

//-----------------------------------------------------------------------------------------
void VstXSynth::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	float* out1 = outputs[0];
	float* out2 = outputs[1];

	if (noteIsOn)
	{
		float baseFreq = freqtab[currentNote & 0x7f] * fScaler;
		float freq1 = baseFreq + fFreq1;	// not really linear...
		float freq2 = baseFreq + fFreq2;
		float* wave1 = (fWaveform1 < .5) ? sawtooth : pulse;
		float* wave2 = (fWaveform2 < .5) ? sawtooth : pulse;
		float wsf = (float)kWaveSize;
		float vol = (float)(fVolume * (double)currentVelocity * midiScaler);
		VstInt32 mask = kWaveSize - 1;
		
		if (currentDelta > 0)
		{
			if (currentDelta >= sampleFrames)	// future
			{
				currentDelta -= sampleFrames;
				return;
			}
			memset (out1, 0, currentDelta * sizeof (float));
			memset (out2, 0, currentDelta * sizeof (float));
			out1 += currentDelta;
			out2 += currentDelta;
			sampleFrames -= currentDelta;
			currentDelta = 0;
		}

		// loop
		while (--sampleFrames >= 0)
		{
			// this is all very raw, there is no means of interpolation,
			// and we will certainly get aliasing due to non-bandlimited
			// waveforms. don't use this for serious projects...
			(*out1++) = wave1[(VstInt32)fPhase1 & mask] * fVolume1 * vol;
			(*out2++) = wave2[(VstInt32)fPhase2 & mask] * fVolume2 * vol;
			fPhase1 += freq1;
			fPhase2 += freq2;
		}
	}						
	else
	{
		memset (out1, 0, sampleFrames * sizeof (float));
		memset (out2, 0, sampleFrames * sizeof (float));
	}
}

//-----------------------------------------------------------------------------------------
VstInt32 VstXSynth::processEvents (VstEvents* ev)
{
	for (VstInt32 i = 0; i < ev->numEvents; i++)
	{
		if ((ev->events[i])->type != kVstMidiType)
			continue;

		VstMidiEvent* event = (VstMidiEvent*)ev->events[i];
		char* midiData = event->midiData;
		VstInt32 status = midiData[0] & 0xf0;	// ignoring channel
		if (status == 0x90 || status == 0x80)	// we only look at notes
		{
			VstInt32 note = midiData[1] & 0x7f;
			VstInt32 velocity = midiData[2] & 0x7f;
			if (status == 0x80)
				velocity = 0;	// note off by velocity 0
			if (!velocity && (note == currentNote))
				noteOff ();
			else
				noteOn (note, velocity, event->deltaFrames);
		}
		else if (status == 0xb0)
		{
			if (midiData[1] == 0x7e || midiData[1] == 0x7b)	// all notes off
				noteOff ();
		}
		event++;
	}
	return 1;
}

//-----------------------------------------------------------------------------------------
void VstXSynth::noteOn (VstInt32 note, VstInt32 velocity, VstInt32 delta)
{
	currentNote = note;
	currentVelocity = velocity;
	currentDelta = delta;
	noteIsOn = true;
	fPhase1 = fPhase2 = 0;
}

//-----------------------------------------------------------------------------------------
void VstXSynth::noteOff ()
{
	noteIsOn = false;
}

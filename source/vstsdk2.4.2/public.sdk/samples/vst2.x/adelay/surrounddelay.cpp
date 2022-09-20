//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:27 $
//
// Category     : VST 2.x SDK Samples
// Filename     : surrounddelay.cpp
// Created by   : Steinberg Media Technologies
// Description  : Simple Surround Delay plugin with Editor using VSTGUI
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __surrounddelay__
#include "surrounddelay.h"
#endif

#ifndef __sdeditor__
#include "editor/sdeditor.h"
#endif

#include <string.h>
#include <stdio.h>

//-------------------------------------------------------------------------------------------------------
AudioEffect* createEffectInstance (audioMasterCallback audioMaster)
{
	return new SurroundDelay (audioMaster);
}

//-----------------------------------------------------------------------------
SurroundDelay::SurroundDelay (audioMasterCallback audioMaster)
: ADelay (audioMaster)
, plugInput (0)
, plugOutput (0)
{
	
	// The first buffer is allocated in ADelay's constructor
	for (int i = 1; i < MAX_CHANNELS; i++)
	{
		sBuffers[i] = new float[size];
	}
	
	setNumInputs (MAX_CHANNELS);
	setNumOutputs (MAX_CHANNELS);

	// We initialize the arrangements to default values.
	// Nevertheless, the host should modify them via
	// appropriate calls to setSpeakerArrangement.
	allocateArrangement (&plugInput, MAX_CHANNELS);
	plugInput->type = kSpeakerArr51;

	allocateArrangement (&plugOutput, MAX_CHANNELS);
	plugOutput->type = kSpeakerArr51;

	setUniqueID ('SDlE');	// this should be unique, use the Steinberg web page for plugin Id registration

	// create the editor
	editor = new SDEditor (this);

	resume ();
}

//-----------------------------------------------------------------------------
SurroundDelay::~SurroundDelay ()
{
	sBuffers[0] = 0;
	// We let ~ADelay delete "buffer"...
	for (int i = 1; i < MAX_CHANNELS; i++)
	{
		if (sBuffers[i])
		{
			delete[] sBuffers[i];
		}
		sBuffers[i] = 0;
	}
	
	deallocateArrangement (&plugInput);
	deallocateArrangement (&plugOutput);
}

//------------------------------------------------------------------------
void SurroundDelay::resume ()
{
	memset (buffer, 0, size * sizeof (float));
	sBuffers[0] = buffer;

	for (int i = 1; i < MAX_CHANNELS; i++)
	{
		memset (sBuffers[i], 0, size * sizeof (float));
	}
}

//------------------------------------------------------------------------
bool SurroundDelay::getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput)
{
	*pluginInput  = plugInput;
	*pluginOutput = plugOutput;
	return true;
}

//------------------------------------------------------------------------
bool SurroundDelay::setSpeakerArrangement (VstSpeakerArrangement* pluginInput,
									 VstSpeakerArrangement* pluginOutput)
{
	if (!pluginOutput || !pluginInput)
		return false;

	bool result = true;
	
	// This plug-in can act on any speaker arrangement,
	// provided that there are the same number of inputs/outputs.
	if (pluginInput->numChannels > MAX_CHANNELS)
	{
		// This plug-in can't have so many channels. So we answer
		// false, and we set the input arrangement with the maximum
		// number of channels possible
		result = false;
		allocateArrangement (&plugInput, MAX_CHANNELS);
		plugInput->type = kSpeakerArr51;
	}
	else
	{
		matchArrangement (&plugInput, pluginInput);
	}
	
	if (pluginOutput->numChannels != plugInput->numChannels)
	{
		// This plug-in only deals with symetric IO configurations...
		result = false;
		matchArrangement (&plugOutput, plugInput);
	}
	else
	{
		matchArrangement (&plugOutput, pluginOutput);
	}

	return result;
}

//------------------------------------------------------------------------
void SurroundDelay::processReplacing (float** inputs, float** outputs, VstInt32 sampleframes)
{
	float* inputs2[1];
	float* outputs2[2];

	outputs2[1] = NULL;

	long cursorTemp = cursor;

	for (int i = 0; i < plugInput->numChannels; i++)
	{
		cursor = cursorTemp;
		
		buffer = sBuffers[i];

		inputs2[0] = inputs[i];
		outputs2[0] = outputs[i];
		
		ADelay::processReplacing (inputs2, outputs2, sampleframes);
	}

	buffer = sBuffers[0];
}

//-----------------------------------------------------------------------------
void SurroundDelay::setParameter (VstInt32 index, float value)
{
	ADelay::setParameter (index, value);

	if (editor)
		((AEffGUIEditor*)editor)->setParameter (index, value);
}

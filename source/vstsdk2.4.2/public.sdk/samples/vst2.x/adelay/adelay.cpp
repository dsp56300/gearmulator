//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:27 $
//
// Category     : VST 2.x SDK Samples
// Filename     : adelay.cpp
// Created by   : Steinberg Media Technologies
// Description  : Simple Delay plugin (Mono->Stereo)
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#ifndef __adelay__
#include "adelay.h"
#endif

//----------------------------------------------------------------------------- 
ADelayProgram::ADelayProgram ()
{
	// default Program Values
	fDelay = 0.5;
	fFeedBack = 0.5;
	fOut = 0.75;

	strcpy (name, "Init");
}

//-----------------------------------------------------------------------------
ADelay::ADelay (audioMasterCallback audioMaster)
	: AudioEffectX (audioMaster, kNumPrograms, kNumParams)
{
	// init
	size = 44100;
	cursor = 0;
	delay = 0;
	buffer = new float[size];
	
	programs = new ADelayProgram[numPrograms];
	fDelay = fFeedBack = fOut = 0;

	if (programs)
		setProgram (0);

	setNumInputs (1);	// mono input
	setNumOutputs (2);	// stereo output

	setUniqueID ('ADly');	// this should be unique, use the Steinberg web page for plugin Id registration

	resume ();		// flush buffer
}

//------------------------------------------------------------------------
ADelay::~ADelay ()
{
	if (buffer)
		delete[] buffer;
	if (programs)
		delete[] programs;
}

//------------------------------------------------------------------------
void ADelay::setProgram (VstInt32 program)
{
	ADelayProgram* ap = &programs[program];

	curProgram = program;
	setParameter (kDelay, ap->fDelay);	
	setParameter (kFeedBack, ap->fFeedBack);
	setParameter (kOut, ap->fOut);
}

//------------------------------------------------------------------------
void ADelay::setDelay (float fdelay)
{
	fDelay = fdelay;
	programs[curProgram].fDelay = fdelay;
	cursor = 0;
	delay = (long)(fdelay * (float)(size - 1));
}

//------------------------------------------------------------------------
void ADelay::setProgramName (char *name)
{
	strcpy (programs[curProgram].name, name);
}

//------------------------------------------------------------------------
void ADelay::getProgramName (char *name)
{
	if (!strcmp (programs[curProgram].name, "Init"))
		sprintf (name, "%s %d", programs[curProgram].name, curProgram + 1);
	else
		strcpy (name, programs[curProgram].name);
}

//-----------------------------------------------------------------------------------------
bool ADelay::getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text)
{
	if (index < kNumPrograms)
	{
		strcpy (text, programs[index].name);
		return true;
	}
	return false;
}

//------------------------------------------------------------------------
void ADelay::resume ()
{
	memset (buffer, 0, size * sizeof (float));
	AudioEffectX::resume ();
}

//------------------------------------------------------------------------
void ADelay::setParameter (VstInt32 index, float value)
{
	ADelayProgram* ap = &programs[curProgram];

	switch (index)
	{
		case kDelay :    setDelay (value);					break;
		case kFeedBack : fFeedBack = ap->fFeedBack = value; break;
		case kOut :      fOut = ap->fOut = value;			break;
	}
}

//------------------------------------------------------------------------
float ADelay::getParameter (VstInt32 index)
{
	float v = 0;

	switch (index)
	{
		case kDelay :    v = fDelay;	break;
		case kFeedBack : v = fFeedBack; break;
		case kOut :      v = fOut;		break;
	}
	return v;
}

//------------------------------------------------------------------------
void ADelay::getParameterName (VstInt32 index, char *label)
{
	switch (index)
	{
		case kDelay :    strcpy (label, "Delay");		break;
		case kFeedBack : strcpy (label, "FeedBack");	break;
		case kOut :      strcpy (label, "Volume");		break;
	}
}

//------------------------------------------------------------------------
void ADelay::getParameterDisplay (VstInt32 index, char *text)
{
	switch (index)
	{
		case kDelay :    int2string (delay, text, kVstMaxParamStrLen);			break;
		case kFeedBack : float2string (fFeedBack, text, kVstMaxParamStrLen);	break;
		case kOut :      dB2string (fOut, text, kVstMaxParamStrLen);			break;
	}
}

//------------------------------------------------------------------------
void ADelay::getParameterLabel (VstInt32 index, char *label)
{
	switch (index)
	{
		case kDelay :    strcpy (label, "samples");	break;
		case kFeedBack : strcpy (label, "amount");	break;
		case kOut :      strcpy (label, "dB");		break;
	}
}

//------------------------------------------------------------------------
bool ADelay::getEffectName (char* name)
{
	strcpy (name, "Delay");
	return true;
}

//------------------------------------------------------------------------
bool ADelay::getProductString (char* text)
{
	strcpy (text, "Delay");
	return true;
}

//------------------------------------------------------------------------
bool ADelay::getVendorString (char* text)
{
	strcpy (text, "Steinberg Media Technologies");
	return true;
}

//---------------------------------------------------------------------------
void ADelay::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	float* in = inputs[0];
	float* out1 = outputs[0];
	float* out2 = outputs[1];

	while (--sampleFrames >= 0)
	{
		float x = *in++;
		float y = buffer[cursor];
		buffer[cursor++] = x + y * fFeedBack;
		if (cursor >= delay)
			cursor = 0;
		*out1++ = y;
		if (out2)
			*out2++ = y;
	}
}

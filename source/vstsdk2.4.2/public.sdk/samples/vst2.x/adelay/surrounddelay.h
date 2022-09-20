//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:27 $
//
// Category     : VST 2.x SDK Samples
// Filename     : surrounddelay.h
// Created by   : Steinberg Media Technologies
// Description  : Simple Surround Delay plugin with Editor using VSTGUI
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __surrounddelay__
#define __surrounddelay__

#ifndef __adelay__
#include "adelay.h"
#endif

#define MAX_CHANNELS 6  // maximun number of channel

//------------------------------------------------------------------------
// SurroundDelay declaration
//------------------------------------------------------------------------
class SurroundDelay : public ADelay
{
public:
	SurroundDelay (audioMasterCallback audioMaster);
	~SurroundDelay ();

	virtual void processReplacing (float** inputs, float** outputs, VstInt32 sampleframes);

	void setParameter (VstInt32 index, float value);

	// functions VST version 2
	virtual bool getVendorString (char* text) { if (text) strcpy (text, "Steinberg"); return true; }
	virtual bool getProductString (char* text) { if (text) strcpy (text, "SDelay"); return true; }
	virtual VstInt32 getVendorVersion () { return 1000; }

	virtual VstPlugCategory getPlugCategory () { return kPlugSurroundFx; }
	
	virtual bool getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput);
	virtual bool setSpeakerArrangement (VstSpeakerArrangement* pluginInput, VstSpeakerArrangement* pluginOutput);

	virtual void resume ();

private:
	VstSpeakerArrangement* plugInput;
	VstSpeakerArrangement* plugOutput;

	float* sBuffers[MAX_CHANNELS];
};

#endif

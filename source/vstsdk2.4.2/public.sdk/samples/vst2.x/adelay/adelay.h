//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:27 $
//
// Category     : VST 2.x SDK Samples
// Filename     : adelay.h
// Created by   : Steinberg Media Technologies
// Description  : Simple Delay plugin (Mono->Stereo)
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __adelay__
#define __adelay__

#include "public.sdk/source/vst2.x/audioeffectx.h"

enum
{
	// Global
	kNumPrograms = 16,

	// Parameters Tags
	kDelay = 0,
	kFeedBack,
	kOut,

	kNumParams
};

class ADelay;

//------------------------------------------------------------------------
class ADelayProgram
{
friend class ADelay;
public:
	ADelayProgram ();
	~ADelayProgram () {}

private:	
	float fDelay;
	float fFeedBack;
	float fOut;
	char name[24];
};

//------------------------------------------------------------------------
class ADelay : public AudioEffectX
{
public:
	ADelay (audioMasterCallback audioMaster);
	~ADelay ();

	//---from AudioEffect-----------------------
	virtual void processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames);

	virtual void setProgram (VstInt32 program);
	virtual void setProgramName (char* name);
	virtual void getProgramName (char* name);
	virtual bool getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text);
	
	virtual void setParameter (VstInt32 index, float value);
	virtual float getParameter (VstInt32 index);
	virtual void getParameterLabel (VstInt32 index, char* label);
	virtual void getParameterDisplay (VstInt32 index, char* text);
	virtual void getParameterName (VstInt32 index, char* text);

	virtual void resume ();

	virtual bool getEffectName (char* name);
	virtual bool getVendorString (char* text);
	virtual bool getProductString (char* text);
	virtual VstInt32 getVendorVersion () { return 1000; }
	
	virtual VstPlugCategory getPlugCategory () { return kPlugCategEffect; }

protected:
	void setDelay (float delay);

	ADelayProgram* programs;
	
	float* buffer;
	float fDelay;
	float fFeedBack;
	float fOut;
	
	long delay;
	long size;
	long cursor;
};

#endif

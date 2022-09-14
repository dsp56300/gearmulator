//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:28 $
//
// Category     : VST 2.x SDK Samples
// Filename     : minihost.cpp
// Created by   : Steinberg
// Description  : VST Mini Host
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#include "pluginterfaces/vst2.x/aeffectx.h"

#if _WIN32
#include <windows.h>
#elif TARGET_API_MAC_CARBON
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <stdio.h>

//-------------------------------------------------------------------------------------------------------
static const VstInt32 kBlockSize = 512;
static const float kSampleRate = 48000.f;
static const VstInt32 kNumProcessCycles = 5;

//-------------------------------------------------------------------------------------------------------
typedef AEffect* (*PluginEntryProc) (audioMasterCallback audioMaster);
static VstIntPtr VSTCALLBACK HostCallback (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

//-------------------------------------------------------------------------------------------------------
// PluginLoader
//-------------------------------------------------------------------------------------------------------
struct PluginLoader
{
//-------------------------------------------------------------------------------------------------------
	void* module;

	PluginLoader ()
	: module (0)
	{}

	~PluginLoader ()
	{
		if (module)
		{
		#if _WIN32
			FreeLibrary ((HMODULE)module);
		#elif TARGET_API_MAC_CARBON
			CFBundleUnloadExecutable ((CFBundleRef)module);
			CFRelease ((CFBundleRef)module);
		#endif
		}
	}

	bool loadLibrary (const char* fileName)
	{
	#if _WIN32
		module = LoadLibrary (fileName);
	#elif TARGET_API_MAC_CARBON
		CFStringRef fileNameString = CFStringCreateWithCString (NULL, fileName, kCFStringEncodingUTF8);
		if (fileNameString == 0)
			return false;
		CFURLRef url = CFURLCreateWithFileSystemPath (NULL, fileNameString, kCFURLPOSIXPathStyle, false);
		CFRelease (fileNameString);
		if (url == 0)
			return false;
		module = CFBundleCreate (NULL, url);
		CFRelease (url);
		if (module && CFBundleLoadExecutable ((CFBundleRef)module) == false)
			return false;
	#endif
		return module != 0;
	}

	PluginEntryProc getMainEntry ()
	{
		PluginEntryProc mainProc = 0;
	#if _WIN32
		mainProc = (PluginEntryProc)GetProcAddress ((HMODULE)module, "VSTPluginMain");
		if (!mainProc)
			mainProc = (PluginEntryProc)GetProcAddress ((HMODULE)module, "main");
	#elif TARGET_API_MAC_CARBON
		mainProc = (PluginEntryProc)CFBundleGetFunctionPointerForName ((CFBundleRef)module, CFSTR("VSTPluginMain"));
		if (!mainProc)
			mainProc = (PluginEntryProc)CFBundleGetFunctionPointerForName ((CFBundleRef)module, CFSTR("main_macho"));
	#endif
		return mainProc;
	}
//-------------------------------------------------------------------------------------------------------
};

//-------------------------------------------------------------------------------------------------------
static bool checkPlatform ()
{
#if VST_64BIT_PLATFORM
	printf ("*** This is a 64 Bit Build! ***\n");
#else
	printf ("*** This is a 32 Bit Build! ***\n");
#endif

	int sizeOfVstIntPtr = sizeof (VstIntPtr);
	int sizeOfVstInt32 = sizeof (VstInt32);
	int sizeOfPointer = sizeof (void*);
	int sizeOfAEffect = sizeof (AEffect);
	
	printf ("VstIntPtr = %d Bytes, VstInt32 = %d Bytes, Pointer = %d Bytes, AEffect = %d Bytes\n\n",
			sizeOfVstIntPtr, sizeOfVstInt32, sizeOfPointer, sizeOfAEffect);

	return sizeOfVstIntPtr == sizeOfPointer;
}

//-------------------------------------------------------------------------------------------------------
static void checkEffectProperties (AEffect* effect);
static void checkEffectProcessing (AEffect* effect);
extern bool checkEffectEditor (AEffect* effect); // minieditor.cpp

//-------------------------------------------------------------------------------------------------------
int main (int argc, char* argv[])
{
	if (!checkPlatform ())
	{
		printf ("Platform verification failed! Please check your Compiler Settings!\n");
		return -1;
	}

	const char* fileName = "again.dll";
	//const char* fileName = "adelay.dll";
	//const char* fileName = "surrounddelay.dll";
	//const char* fileName = "vstxsynth.dll";
	//const char* fileName = "drawtest.dll";

	if (argc > 1)
		fileName = argv[1];

	printf ("HOST> Load library...\n");
	PluginLoader loader;
	if (!loader.loadLibrary (fileName))
	{
		printf ("Failed to load VST Plugin library!\n");
		return -1;
	}

	PluginEntryProc mainEntry = loader.getMainEntry ();
	if (!mainEntry)
	{
		printf ("VST Plugin main entry not found!\n");
		return -1;
	}

	printf ("HOST> Create effect...\n");
	AEffect* effect = mainEntry (HostCallback);
	if (!effect)
	{
		printf ("Failed to create effect instance!\n");
		return -1;
	}

	printf ("HOST> Init sequence...\n");
	effect->dispatcher (effect, effOpen, 0, 0, 0, 0);
	effect->dispatcher (effect, effSetSampleRate, 0, 0, 0, kSampleRate);
	effect->dispatcher (effect, effSetBlockSize, 0, kBlockSize, 0, 0);

	checkEffectProperties (effect);
	checkEffectProcessing (effect);
	checkEffectEditor (effect);

	printf ("HOST> Close effect...\n");
	effect->dispatcher (effect, effClose, 0, 0, 0, 0);
	return 0;
}

//-------------------------------------------------------------------------------------------------------
void checkEffectProperties (AEffect* effect)
{
	printf ("HOST> Gathering properties...\n");

	char effectName[256] = {0};
	char vendorString[256] = {0};
	char productString[256] = {0};

	effect->dispatcher (effect, effGetEffectName, 0, 0, effectName, 0);
	effect->dispatcher (effect, effGetVendorString, 0, 0, vendorString, 0);
	effect->dispatcher (effect, effGetProductString, 0, 0, productString, 0);

	printf ("Name = %s\nVendor = %s\nProduct = %s\n\n", effectName, vendorString, productString);

	printf ("numPrograms = %d\nnumParams = %d\nnumInputs = %d\nnumOutputs = %d\n\n", 
			effect->numPrograms, effect->numParams, effect->numInputs, effect->numOutputs);

	// Iterate programs...
	for (VstInt32 progIndex = 0; progIndex < effect->numPrograms; progIndex++)
	{
		char progName[256] = {0};
		if (!effect->dispatcher (effect, effGetProgramNameIndexed, progIndex, 0, progName, 0))
		{
			effect->dispatcher (effect, effSetProgram, 0, progIndex, 0, 0); // Note: old program not restored here!
			effect->dispatcher (effect, effGetProgramName, 0, 0, progName, 0);
		}
		printf ("Program %03d: %s\n", progIndex, progName);
	}

	printf ("\n");

	// Iterate parameters...
	for (VstInt32 paramIndex = 0; paramIndex < effect->numParams; paramIndex++)
	{
		char paramName[256] = {0};
		char paramLabel[256] = {0};
		char paramDisplay[256] = {0};

		effect->dispatcher (effect, effGetParamName, paramIndex, 0, paramName, 0);
		effect->dispatcher (effect, effGetParamLabel, paramIndex, 0, paramLabel, 0);
		effect->dispatcher (effect, effGetParamDisplay, paramIndex, 0, paramDisplay, 0);
		float value = effect->getParameter (effect, paramIndex);

		printf ("Param %03d: %s [%s %s] (normalized = %f)\n", paramIndex, paramName, paramDisplay, paramLabel, value);
	}

	printf ("\n");

	// Can-do nonsense...
	static const char* canDos[] =
	{
		"receiveVstEvents",
		"receiveVstMidiEvent",
		"midiProgramNames"
	};

	for (VstInt32 canDoIndex = 0; canDoIndex < sizeof (canDos) / sizeof (canDos[0]); canDoIndex++)
	{
		printf ("Can do %s... ", canDos[canDoIndex]);
		VstInt32 result = (VstInt32)effect->dispatcher (effect, effCanDo, 0, 0, (void*)canDos[canDoIndex], 0);
		switch (result)
		{
			case 0  : printf ("don't know"); break;
			case 1  : printf ("yes"); break;
			case -1 : printf ("definitely not!"); break;
			default : printf ("?????");
		}
		printf ("\n");
	}

	printf ("\n");
}

//-------------------------------------------------------------------------------------------------------
void checkEffectProcessing (AEffect* effect)
{
	float** inputs = 0;
	float** outputs = 0;
	VstInt32 numInputs = effect->numInputs;
	VstInt32 numOutputs = effect->numOutputs;
	
	if (numInputs > 0)
	{
		inputs = new float*[numInputs];
		for (VstInt32 i = 0; i < numInputs; i++)
		{
			inputs[i] = new float[kBlockSize];
			memset (inputs[i], 0, kBlockSize * sizeof (float));
		}
	}

	if (numOutputs > 0)
	{
		outputs = new float*[numOutputs];
		for (VstInt32 i = 0; i < numOutputs; i++)
		{
			outputs[i] = new float[kBlockSize];
			memset (outputs[i], 0, kBlockSize * sizeof (float));
		}
	}

	printf ("HOST> Resume effect...\n");
	effect->dispatcher (effect, effMainsChanged, 0, 1, 0, 0);

	for (VstInt32 processCount = 0; processCount < kNumProcessCycles; processCount++)
	{
		printf ("HOST> Process Replacing...\n");
		effect->processReplacing (effect, inputs, outputs, kBlockSize);
	}

	printf ("HOST> Suspend effect...\n");
	effect->dispatcher (effect, effMainsChanged, 0, 0, 0, 0);

	if (numInputs > 0)
	{
		for (VstInt32 i = 0; i < numInputs; i++)
			delete [] inputs[i];
		delete [] inputs;
	}

	if (numOutputs > 0)
	{
		for (VstInt32 i = 0; i < numOutputs; i++)
			delete [] outputs[i];
		delete [] outputs;
	}
}

//-------------------------------------------------------------------------------------------------------
VstIntPtr VSTCALLBACK HostCallback (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
	VstIntPtr result = 0;

	// Filter idle calls...
	bool filtered = false;
	if (opcode == audioMasterIdle)
	{
		static bool wasIdle = false;
		if (wasIdle)
			filtered = true;
		else
		{
			printf ("(Future idle calls will not be displayed!)\n");
			wasIdle = true;
		}
	}

	if (!filtered)
		printf ("PLUG> HostCallback (opcode %d)\n index = %d, value = %p, ptr = %p, opt = %f\n", opcode, index, FromVstPtr<void> (value), ptr, opt);

	switch (opcode)
	{
		case audioMasterVersion :
			result = kVstVersion;
			break;
	}

	return result;
}

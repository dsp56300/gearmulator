//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4       $Date: 2006/06/06 16:01:34 $
//
// Category     : VST 2.x Classes
// Filename     : audioeffect.h
// Created by   : Steinberg Media Technologies
// Description  : Class AudioEffect (VST 1.0)
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __audioeffect__
#define __audioeffect__

#include "pluginterfaces/vst2.x/aeffect.h"	// "c" interface

class AEffEditor;

//-------------------------------------------------------------------------------------------------------
/** VST Effect Base Class (VST 1.0). */
//-------------------------------------------------------------------------------------------------------
class AudioEffect
{
public:
//-------------------------------------------------------------------------------------------------------
	AudioEffect (audioMasterCallback audioMaster, VstInt32 numPrograms, VstInt32 numParams); ///< Create an \e AudioEffect object
	virtual ~AudioEffect (); ///< Destroy an \e AudioEffect object

	virtual VstIntPtr dispatcher (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);	///< Opcodes dispatcher

//-------------------------------------------------------------------------------------------------------
/// \name State Transitions
//-------------------------------------------------------------------------------------------------------
//@{
	virtual void open () {}		///< Called when plug-in is initialized
	virtual void close () {}	///< Called when plug-in will be released
	virtual void suspend () {}	///< Called when plug-in is switched to off
	virtual void resume () {}	///< Called when plug-in is switched to on
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Processing
//-------------------------------------------------------------------------------------------------------
//@{
	virtual void setSampleRate (float sampleRate)  { this->sampleRate = sampleRate; }	///< Called when the sample rate changes (always in a suspend state)
	virtual void setBlockSize (VstInt32 blockSize) { this->blockSize = blockSize; }		///< Called when the Maximun block size changes (always in a suspend state). Note that the sampleFrames in Process Calls could be smaller than this block size, but NOT bigger.

	virtual void processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames) = 0; ///< Process 32 bit (single precision) floats (always in a resume state)

#if VST_2_4_EXTENSIONS
	virtual void processDoubleReplacing (double** inputs, double** outputs, VstInt32 sampleFrames) {} ///< Process 64 bit (double precision) floats (always in a resume state) \sa processReplacing
#endif // VST_2_4_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Parameters
//-------------------------------------------------------------------------------------------------------
//@{
	virtual void setParameter (VstInt32 index, float value) {}	///< Called when a parameter changed
	virtual float getParameter (VstInt32 index) { return 0; }	///< Return the value of the parameter with \e index
	virtual void setParameterAutomated (VstInt32 index, float value);///< Called after a control has changed in the editor and when the associated parameter should be automated
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Programs and Persistence
//-------------------------------------------------------------------------------------------------------
//@{
	virtual VstInt32 getProgram () { return curProgram; }					///< Return the index to the current program
	virtual void setProgram (VstInt32 program) { curProgram = program; }	///< Set the current program to \e program
	
	virtual void setProgramName (char* name) {}				///< Stuff the name field of the current program with \e name. Limited to #kVstMaxProgNameLen.
	virtual void getProgramName (char* name) { *name = 0; }	///< Stuff \e name with the name of the current program. Limited to #kVstMaxProgNameLen.
	
	virtual void getParameterLabel (VstInt32 index, char* label)  { *label = 0; }	///< Stuff \e label with the units in which parameter \e index is displayed (i.e. "sec", "dB", "type", etc...). Limited to #kVstMaxParamStrLen.
	virtual void getParameterDisplay (VstInt32 index, char* text) { *text = 0; }	///< Stuff \e text with a string representation ("0.5", "-3", "PLATE", etc...) of the value of parameter \e index. Limited to #kVstMaxParamStrLen.
	virtual void getParameterName (VstInt32 index, char* text)    { *text = 0; }    ///< Stuff \e text with the name ("Time", "Gain", "RoomType", etc...) of parameter \e index. Limited to #kVstMaxParamStrLen.
	
	virtual VstInt32 getChunk (void** data, bool isPreset = false) { return 0; } ///< Host stores plug-in state. Returns the size in bytes of the chunk (plug-in allocates the data array)
	virtual VstInt32 setChunk (void* data, VstInt32 byteSize, bool isPreset = false) { return 0; }	///< Host restores plug-in state
//@}
	
//-------------------------------------------------------------------------------------------------------
/// \name Internal Setup
//-------------------------------------------------------------------------------------------------------
//@{
	virtual void setUniqueID (VstInt32 iD)        { cEffect.uniqueID = iD; }		///< Must be called to set the plug-ins unique ID!
	virtual void setNumInputs (VstInt32 inputs)   { cEffect.numInputs = inputs; }	///< Set the number of inputs the plug-in will handle. For a plug-in which could change its IO configuration, this number is the maximun available inputs.
	virtual void setNumOutputs (VstInt32 outputs) { cEffect.numOutputs = outputs; }	///< Set the number of outputs the plug-in will handle. For a plug-in which could change its IO configuration, this number is the maximun available ouputs.
	virtual void setNumPrograms(VstInt32 count) { cEffect.numPrograms = count; numPrograms = count; }

	virtual void canProcessReplacing (bool state = true);	///< Tells that processReplacing() could be used. Mandatory in VST 2.4!

#if VST_2_4_EXTENSIONS
	virtual void canDoubleReplacing (bool state = true);	///< Tells that processDoubleReplacing() is implemented.
#endif // VST_2_4_EXTENSIONS

	virtual void programsAreChunks (bool state = true);	///< Program data is handled in formatless chunks (using getChunk-setChunks)
	virtual void setInitialDelay (VstInt32 delay);		///< Use to report the plug-in's latency (Group Delay)
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Editor
//-------------------------------------------------------------------------------------------------------
//@{
	void setEditor (AEffEditor* editor);				///< Should be called if you want to define your own editor
	virtual AEffEditor* getEditor () { return editor; }	///< Returns the attached editor
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Inquiry
//-------------------------------------------------------------------------------------------------------
//@{
	virtual AEffect* getAeffect ()   { return &cEffect; }		///< Returns the #AEffect structure
	virtual float getSampleRate ()   { return sampleRate; }		///< Returns the current sample rate
	virtual VstInt32 getBlockSize () { return blockSize; }		///< Returns the current Maximum block size
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Host Communication
//-------------------------------------------------------------------------------------------------------
//@{
	virtual VstInt32 getMasterVersion ();		///< Returns the Host's version (for example 2400 for VST 2.4)
	virtual VstInt32 getCurrentUniqueId ();		///< Returns current unique identifier when loading shell plug-ins
	virtual void masterIdle ();					///< Give idle time to Host application
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Tools (helpers)
//-------------------------------------------------------------------------------------------------------
//@{
	virtual void dB2string (float value, char* text, VstInt32 maxLen);		///< Stuffs \e text with an amplitude on the [0.0, 1.0] scale converted to its value in decibels.
	virtual void Hz2string (float samples, char* text, VstInt32 maxLen);	///< Stuffs \e text with the frequency in Hertz that has a period of \e samples.
	virtual void ms2string (float samples, char* text, VstInt32 maxLen);	///< Stuffs \e text with the duration in milliseconds of \e samples frames.
	virtual void float2string (float value, char* text, VstInt32 maxLen);	///< Stuffs \e text with a string representation on the floating point \e value.
	virtual void int2string (VstInt32 value, char* text, VstInt32 maxLen);	///< Stuffs \e text with a string representation on the integer \e value.
//@}

//-------------------------------------------------------------------------------------------------------
// Deprecated methods
//-------------------------------------------------------------------------------------------------------
/// @cond ignore
	virtual void DECLARE_VST_DEPRECATED (process) (float** inputs, float** outputs, VstInt32 sampleFrames) {}
	virtual float DECLARE_VST_DEPRECATED (getVu) () { return 0; }
	virtual void DECLARE_VST_DEPRECATED (hasVu) (bool state = true);
	virtual void DECLARE_VST_DEPRECATED (hasClip) (bool state = true);
	virtual void DECLARE_VST_DEPRECATED (canMono) (bool state = true);
	virtual void DECLARE_VST_DEPRECATED (setRealtimeQualities) (VstInt32 qualities);
	virtual void DECLARE_VST_DEPRECATED (setOfflineQualities) (VstInt32 qualities);
	virtual bool DECLARE_VST_DEPRECATED (isInputConnected) (VstInt32 input);
	virtual bool DECLARE_VST_DEPRECATED (isOutputConnected) (VstInt32 output);
/// @endcond

//-------------------------------------------------------------------------------------------------------
protected:	
	audioMasterCallback audioMaster;	///< Host callback
	AEffEditor* editor;					///< Pointer to the plug-in's editor
	float sampleRate;					///< Current sample rate
	VstInt32 blockSize;					///< Maximum block size
	VstInt32 numPrograms;				///< Number of programs
	VstInt32 numParams;					///< Number of parameters
	VstInt32 curProgram;				///< Current program
	AEffect  cEffect;					///< #AEffect object

/// @cond ignore
	static VstIntPtr dispatchEffectClass (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
	static float getParameterClass (AEffect* e, VstInt32 index);
	static void setParameterClass (AEffect* e, VstInt32 index, float value);
	static void DECLARE_VST_DEPRECATED (processClass) (AEffect* e, float** inputs, float** outputs, VstInt32 sampleFrames);
	static void processClassReplacing (AEffect* e, float** inputs, float** outputs, VstInt32 sampleFrames);

#if VST_2_4_EXTENSIONS
	static void processClassDoubleReplacing (AEffect* e, double** inputs, double** outputs, VstInt32 sampleFrames);
#endif // VST_2_4_EXTENSIONS
/// @endcond
};

#endif // __audioeffect__

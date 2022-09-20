//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4       $Date: 2006/06/07 08:22:01 $
//
// Category     : VST 2.x Classes
// Filename     : audioeffect.cpp
// Created by   : Steinberg Media Technologies
// Description  : Class AudioEffect (VST 1.0)
//
// � 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#include "audioeffect.h"
#include "aeffeditor.h"

#include <stddef.h>
#include <stdio.h>
#include <math.h>

//-------------------------------------------------------------------------------------------------------
VstIntPtr AudioEffect::dispatchEffectClass (AEffect* e, VstInt32 opCode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
	AudioEffect* ae = (AudioEffect*)(e->object);

	if (opCode == effClose)
	{
		ae->dispatcher (opCode, index, value, ptr, opt);
		delete ae;
		return 1;
	}

	return ae->dispatcher (opCode, index, value, ptr, opt);
}

//-------------------------------------------------------------------------------------------------------
float AudioEffect::getParameterClass (AEffect* e, VstInt32 index)
{
	AudioEffect* ae = (AudioEffect*)(e->object);
	return ae->getParameter (index);
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::setParameterClass (AEffect* e, VstInt32 index, float value)
{
	AudioEffect* ae = (AudioEffect*)(e->object);
	ae->setParameter (index, value);
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::DECLARE_VST_DEPRECATED (processClass) (AEffect* e, float** inputs, float** outputs, VstInt32 sampleFrames)
{
	AudioEffect* ae = (AudioEffect*)(e->object);
	ae->DECLARE_VST_DEPRECATED (process) (inputs, outputs, sampleFrames);
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::processClassReplacing (AEffect* e, float** inputs, float** outputs, VstInt32 sampleFrames)
{
	AudioEffect* ae = (AudioEffect*)(e->object);
	ae->processReplacing (inputs, outputs, sampleFrames);
}

//-------------------------------------------------------------------------------------------------------
#if VST_2_4_EXTENSIONS
void AudioEffect::processClassDoubleReplacing (AEffect* e, double** inputs, double** outputs, VstInt32 sampleFrames)
{
	AudioEffect* ae = (AudioEffect*)(e->object);
	ae->processDoubleReplacing (inputs, outputs, sampleFrames);
}
#endif

//-------------------------------------------------------------------------------------------------------
// Class AudioEffect Implementation
//-------------------------------------------------------------------------------------------------------
/*!
	The constructor of your class is passed a parameter of the type \e audioMasterCallback. The actual 
	mechanism in which your class gets constructed is not important right now. Effectively your class is 
	constructed by the hosting application, which passes an object of type \e audioMasterCallback that 
	handles the interaction with the plug-in. You pass this on to the base class' constructor and then 
	can forget about it.

	\param audioMaster Passed by the Host and handles interaction
	\param numPrograms Pass the number of programs the plug-in provides
	\param numParams Pass the number of parameters the plug-in provides
	
\code
MyPlug::MyPlug (audioMasterCallback audioMaster)
: AudioEffectX (audioMaster, 1, 1)    // 1 program, 1 parameter only
{
	setNumInputs (2); // stereo in
	setNumOutputs (2); // stereo out
	setUniqueID ('MyPl'); // you must change this for other plug-ins!
	canProcessReplacing (); // supports replacing mode
}
\endcode

	\sa setNumInputs, setNumOutputs, setUniqueID, canProcessReplacing
*/
AudioEffect::AudioEffect (audioMasterCallback audioMaster, VstInt32 numPrograms, VstInt32 numParams)
: audioMaster (audioMaster)
, editor (0)
, sampleRate (44100.f)
, blockSize (1024)
, numPrograms (numPrograms)
, numParams (numParams)
, curProgram (0)
{
	memset (&cEffect, 0, sizeof (cEffect));

	cEffect.magic = kEffectMagic;
	cEffect.dispatcher = dispatchEffectClass;
	cEffect.DECLARE_VST_DEPRECATED (process) = DECLARE_VST_DEPRECATED (processClass);
	cEffect.setParameter = setParameterClass;
	cEffect.getParameter = getParameterClass;
	cEffect.numPrograms  = numPrograms;
	cEffect.numParams    = numParams;
	cEffect.numInputs  = 1;		// mono input
	cEffect.numOutputs = 2;		// stereo output
	cEffect.DECLARE_VST_DEPRECATED (ioRatio) = 1.f;
	cEffect.object = this;
	cEffect.uniqueID = CCONST ('N', 'o', 'E', 'f');
	cEffect.version  = 1;
	cEffect.processReplacing = processClassReplacing;

#if VST_2_4_EXTENSIONS
	canProcessReplacing (); // mandatory in VST 2.4!
	cEffect.processDoubleReplacing = processClassDoubleReplacing;
#endif
}

//-------------------------------------------------------------------------------------------------------
AudioEffect::~AudioEffect ()
{
	if (editor)
		delete editor;
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::setEditor (AEffEditor* editor)
{
	this->editor = editor;
	if (editor) 
		cEffect.flags |= effFlagsHasEditor;
	else 
		cEffect.flags &= ~effFlagsHasEditor;
}

//-------------------------------------------------------------------------------------------------------
VstIntPtr AudioEffect::dispatcher (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
	VstIntPtr v = 0;
	
	switch (opcode)
	{
		case effOpen:				open ();											break;
		case effClose:				close ();											break;
		case effSetProgram:			if (value < numPrograms) setProgram ((VstInt32)value); break;
		case effGetProgram:			v = getProgram ();									break;
		case effSetProgramName: 	setProgramName ((char*)ptr);						break;
		case effGetProgramName: 	getProgramName ((char*)ptr);						break;
		case effGetParamLabel:		getParameterLabel (index, (char*)ptr);				break;
		case effGetParamDisplay:	getParameterDisplay (index, (char*)ptr);			break;
		case effGetParamName:		getParameterName (index, (char*)ptr);				break;

		case effSetSampleRate:		setSampleRate (opt);								break;
		case effSetBlockSize:		setBlockSize ((VstInt32)value);						break;
		case effMainsChanged:		if (!value) suspend (); else resume ();				break;
	#if !VST_FORCE_DEPRECATED
		case effGetVu:				v = (VstIntPtr)(getVu () * 32767.);					break;
	#endif

		//---Editor------------
		case effEditGetRect:		if (editor) v = editor->getRect ((ERect**)ptr) ? 1 : 0;	break;
		case effEditOpen:			if (editor) v = editor->open (ptr) ? 1 : 0;			break;
		case effEditClose:			if (editor) editor->close ();						break;		
		case effEditIdle:			if (editor) editor->idle ();						break;
		
	#if (TARGET_API_MAC_CARBON && !VST_FORCE_DEPRECATED)
		case effEditDraw:			if (editor) editor->draw ((ERect*)ptr);				break;
		case effEditMouse:			if (editor) v = editor->mouse (index, value);		break;
		case effEditKey:			if (editor) v = editor->key (value);				break;
		case effEditTop:			if (editor) editor->top ();							break;
		case effEditSleep:			if (editor) editor->sleep ();						break;
	#endif
		
		case DECLARE_VST_DEPRECATED (effIdentify):	v = CCONST ('N', 'v', 'E', 'f');	break;

		//---Persistence-------
		case effGetChunk:			v = getChunk ((void**)ptr, index ? true : false);	break;
		case effSetChunk:			v = setChunk (ptr, (VstInt32)value, index ? true : false);	break;
	}
	return v;
}

//-------------------------------------------------------------------------------------------------------
/*!
	Use to ask for the Host's version
	\return The Host's version
*/
VstInt32 AudioEffect::getMasterVersion ()
{
	VstInt32 version = 1;
	if (audioMaster)
	{
		version = (VstInt32)audioMaster (&cEffect, audioMasterVersion, 0, 0, 0, 0);
		if (!version)	// old
			version = 1;
	}
	return version;
}

//-------------------------------------------------------------------------------------------------------
/*!
	\sa AudioEffectX::getNextShellPlugin
*/
VstInt32 AudioEffect::getCurrentUniqueId ()
{
	VstInt32 id = 0;
	if (audioMaster)
		id = (VstInt32)audioMaster (&cEffect, audioMasterCurrentId, 0, 0, 0, 0);
	return id;
}

//-------------------------------------------------------------------------------------------------------
/*!
	Give idle time to Host application, e.g. if plug-in editor is doing mouse tracking in a modal loop.
*/
void AudioEffect::masterIdle ()
{
	if (audioMaster)
		audioMaster (&cEffect, audioMasterIdle, 0, 0, 0, 0);
}

//-------------------------------------------------------------------------------------------------------
bool AudioEffect::DECLARE_VST_DEPRECATED (isInputConnected) (VstInt32 input)
{
	VstInt32 ret = 0;
	if (audioMaster)
		ret = (VstInt32)audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterPinConnected), input, 0, 0, 0);
	return ret ? false : true;		// return value is 0 for true
}

//-------------------------------------------------------------------------------------------------------
bool AudioEffect::DECLARE_VST_DEPRECATED (isOutputConnected) (VstInt32 output)
{
	VstInt32 ret = 0;
	if (audioMaster)
		ret = (VstInt32)audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterPinConnected), output, 1, 0, 0);
	return ret ? false : true;		// return value is 0 for true
}

//-------------------------------------------------------------------------------------------------------
/*!
	\param index parameter index
	\param float parameter value

	\note An important thing to notice is that if the user changes a parameter in your editor, which is 
	out of the Host's control if you are not using the default string based interface, you should 
	call setParameterAutomated (). This ensures that the Host is notified of the parameter change, which 
	allows it to record these changes for automation.

	\sa setParameter
*/
void AudioEffect::setParameterAutomated (VstInt32 index, float value)
{
	setParameter (index, value);
	if (audioMaster)
		audioMaster (&cEffect, audioMasterAutomate, index, 0, 0, value);	// value is in opt
}

//-------------------------------------------------------------------------------------------------------
// Flags
//-------------------------------------------------------------------------------------------------------
void AudioEffect::DECLARE_VST_DEPRECATED (hasVu) (bool state)
{
	if (state)
		cEffect.flags |= DECLARE_VST_DEPRECATED (effFlagsHasVu);
	else
		cEffect.flags &= ~DECLARE_VST_DEPRECATED (effFlagsHasVu);
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::DECLARE_VST_DEPRECATED (hasClip) (bool state)
{
	if (state)
		cEffect.flags |= DECLARE_VST_DEPRECATED (effFlagsHasClip);
	else
		cEffect.flags &= ~DECLARE_VST_DEPRECATED (effFlagsHasClip);
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::DECLARE_VST_DEPRECATED (canMono) (bool state)
{
	if (state)
		cEffect.flags |= DECLARE_VST_DEPRECATED (effFlagsCanMono);
	else
		cEffect.flags &= ~DECLARE_VST_DEPRECATED (effFlagsCanMono);
}

//-------------------------------------------------------------------------------------------------------
/*!
	\param state Set to \e true if supported

	\note Needs to be called in the plug-in's constructor
*/
void AudioEffect::canProcessReplacing (bool state)
{
	if (state)
		cEffect.flags |= effFlagsCanReplacing;
	else
		cEffect.flags &= ~effFlagsCanReplacing;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\param state Set to \e true if supported

	\note Needs to be called in the plug-in's constructor
*/
#if VST_2_4_EXTENSIONS
void AudioEffect::canDoubleReplacing (bool state)
{
	if (state)
		cEffect.flags |= effFlagsCanDoubleReplacing;
	else
		cEffect.flags &= ~effFlagsCanDoubleReplacing;
}
#endif

//-------------------------------------------------------------------------------------------------------
/*!
	\param state Set \e true if programs are chunks

	\note Needs to be called in the plug-in's constructor
*/
void AudioEffect::programsAreChunks (bool state)
{
	if (state)
		cEffect.flags |= effFlagsProgramChunks;
	else
		cEffect.flags &= ~effFlagsProgramChunks;
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::DECLARE_VST_DEPRECATED (setRealtimeQualities) (VstInt32 qualities)
{
	cEffect.DECLARE_VST_DEPRECATED (realQualities) = qualities;
}

//-------------------------------------------------------------------------------------------------------
void AudioEffect::DECLARE_VST_DEPRECATED (setOfflineQualities) (VstInt32 qualities)
{
	cEffect.DECLARE_VST_DEPRECATED (offQualities) = qualities;
}

//-------------------------------------------------------------------------------------------------------
/*!
	Use to report the Plug-in's latency (Group Delay)

	\param delay Plug-ins delay in samples
*/
void AudioEffect::setInitialDelay (VstInt32 delay)
{
	cEffect.initialDelay = delay;
}

//-------------------------------------------------------------------------------------------------------
// Strings Conversion
//-------------------------------------------------------------------------------------------------------
/*!
	\param value Value to convert
	\param text	String up to length char
	\param maxLen Maximal length of the string
*/
void AudioEffect::dB2string (float value, char* text, VstInt32 maxLen)
{
	if (value <= 0)
		vst_strncpy (text, "-oo", maxLen);
	else
		float2string ((float)(20. * log10 (value)), text, maxLen);
}

//-------------------------------------------------------------------------------------------------------
/*!
	\param samples Number of samples
	\param text	String up to length char
	\param maxLen Maximal length of the string
*/
void AudioEffect::Hz2string (float samples, char* text, VstInt32 maxLen)
{
	float sampleRate = getSampleRate ();
	if (!samples)
		float2string (0, text, maxLen);
	else
		float2string (sampleRate / samples, text, maxLen);
}

//-------------------------------------------------------------------------------------------------------
/*!
	\param samples Number of samples
	\param text	String up to length char
	\param maxLen Maximal length of the string
*/
void AudioEffect::ms2string (float samples, char* text, VstInt32 maxLen)
{
	float2string ((float)(samples * 1000. / getSampleRate ()), text, maxLen);
}

//-------------------------------------------------------------------------------------------------------
/*!
	\param value Value to convert
	\param text	String up to length char
	\param maxLen Maximal length of the string
*/
void AudioEffect::float2string (float value, char* text, VstInt32 maxLen)
{
	VstInt32 c = 0, neg = 0;
	char string[32];
	char* s;
	double v, integ, i10, mantissa, m10, ten = 10.;
	
	v = (double)value;
	if (v < 0)
	{
		neg = 1;
		value = -value;
		v = -v;
		c++;
		if (v > 9999999.)
		{
			vst_strncpy (string, "Huge!", 31);
			return;
		}
	}
	else if (v > 99999999.)
	{
		vst_strncpy (string, "Huge!", 31);
		return;
	}

	s = string + 31;
	*s-- = 0;
	*s-- = '.';
	c++;
	
	integ = floor (v);
	i10 = fmod (integ, ten);
	*s-- = (char)((VstInt32)i10 + '0');
	integ /= ten;
	c++;
	while (integ >= 1. && c < 8)
	{
		i10 = fmod (integ, ten);
		*s-- = (char)((VstInt32)i10 + '0');
		integ /= ten;
		c++;
	}
	if (neg)
		*s-- = '-';
	vst_strncpy (text, s + 1, maxLen);
	if (c >= 8)
		return;

	s = string + 31;
	*s-- = 0;
	mantissa = fmod (v, 1.);
	mantissa *= pow (ten, (double)(8 - c));
	while (c < 8)
	{
		if (mantissa <= 0)
			*s-- = '0';
		else
		{
			m10 = fmod (mantissa, ten);
			*s-- = (char)((VstInt32)m10 + '0');
			mantissa /= 10.;
		}
		c++;
	}
	vst_strncat (text, s + 1, maxLen);
}

//-------------------------------------------------------------------------------------------------------
/*!
	\param value Value to convert
	\param text	String up to length char
	\param maxLen Maximal length of the string
*/
void AudioEffect::int2string (VstInt32 value, char* text, VstInt32 maxLen)
{
	if (value >= 100000000)
	{
		vst_strncpy (text, "Huge!", maxLen);
		return;
	}

	if (value < 0)
	{
		vst_strncpy (text, "-", maxLen);
		value = -value;
	}
	else
		vst_strncpy (text, "", maxLen);

	bool state = false;
	for (VstInt32 div = 100000000; div >= 1; div /= 10)
	{
		VstInt32 digit = value / div;
		value -= digit * div;
		if (state || digit > 0)
		{
			char temp[2] = {char('0' + digit), '\0'};
			vst_strncat (text, temp, maxLen);
			state = true;
		}
	}
}
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
	
	This process method must be provided. It takes input data, applies its pocessing algorithm, and then puts the 
	result to the output by overwriting the output buffer.

	\param inputs An array of pointers to the data
	\param outputs An array of pointers to where the data can be written to
	\param sampleFrames Number of sample frames to process

	\warning Never call any Mac OS 9 functions (or other functions which call into the OS) inside your 
	audio process function! This will crash the system when your plug-in is run in MP (multiprocessor) mode.
	If you must call into the OS, you must use MPRemoteCall () (see Apples' documentation), or 
	explicitly use functions which are documented by Apple to be MP safe. On Mac OS X read the system 
	header files to be sure that you only call thread safe functions.

*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setBlockSize (VstInt32 blockSize)

	This is called by the Host, and tells the plug-in that the maximum block size passed to 
	processReplacing() will be \e blockSize.

	\param blockSize Maximum number of sample frames

	\warning You <b>must</b> process <b>exactly</b> \e sampleFrames number of samples in inside processReplacing, not more!
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setParameter (VstInt32 index, float value)

	Parameters are the individual parameter settings the user can adjust. A VST Host can automate these 
	parameters. Set parameter \e index to \e value.

	\param index Index of the parameter to change
	\param value A float value between 0.0 and 1.0 inclusive

	\note Parameter values, like all VST parameters, are declared as floats with an inclusive range of 
	0.0 to 1.0. How data is presented to the user is merely in the user-interface handling. This is a 
	convention, but still worth regarding. Maybe the VST-Host's automation system depends on this range.
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn float AudioEffect::getParameter (VstInt32 index)

	Return the \e value of parameter \e index

	\param index Index of the parameter
	\return A float value between 0.0 and 1.0 inclusive
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::getParameterLabel (VstInt32 index, char* label)

	\param index Index of the parameter
	\param label A string up to 8 char
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::getParameterDisplay (VstInt32 index, char* text)

	\param index Index of the parameter
	\param text A string up to 8 char
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffect::getProgram ()

	\return Index of the current program
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setProgram (VstInt32 program)

	\param Program of the current program
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::getParameterName (VstInt32 index, char* text)

	\param index Index of the parameter
	\param text A string up to 8 char
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setProgramName (char* name)

	The program name is displayed in the rack, and can be edited by the user.

	\param name A string up to 24 char

	\warning Please be aware that the string lengths supported by the default VST interface are normally 
	limited to 24 characters. If you copy too much data into the buffers provided, you will break the 
	Host application.
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::getProgramName (char* name)

	The program name is displayed in the rack, and can be edited by the user.

	\param name A string up to 24 char

	\warning Please be aware that the string lengths supported by the default VST interface are normally 
	limited to 24 characters. If you copy too much data into the buffers provided, you will break the 
	Host application.
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffect::getChunk (void** data, bool isPreset)
	
	\param data should point to the newly allocated memory block containg state data. You can savely release it in next suspend/resume call.
	\param isPreset true when saving a single program, false for all programs

	\note
	If your plug-in is configured to use chunks (see AudioEffect::programsAreChunks), the Host
	will ask for a block of memory describing the current plug-in state for saving.
	To restore the state at a later stage, the same data is passed back to AudioEffect::setChunk.
	Alternatively, when not using chunk, the Host will simply save all parameter values.
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffect::setChunk (void* data, VstInt32 byteSize, bool isPreset)
	
	\param data pointer to state data (owned by Host)
	\param byteSize size of state data
	\param isPreset true when restoring a single program, false for all programs

	\sa getChunk
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setNumInputs (VstInt32 inputs)

	This number is fixed at construction time and can't change until the plug-in is destroyed.

	\param inputs The number of inputs

	\sa isInputConnected()

	\note Needs to be called in the plug-in's constructor
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setNumOutputs (VstInt32 outputs)

	This number is fixed at construction time and can't change until the plug-in is destroyed.

	\param outputs The number of outputs

	\sa isOutputConnected()

	\note Needs to be called in the plug-in's constructor
*/

//-------------------------------------------------------------------------------------------------------
/*!
	\fn void AudioEffect::setUniqueID (VstInt32 iD)

	Must call this! Set the plug-in's unique identifier. The Host uses this to identify the plug-in, for
	instance when it is loading effect programs and banks. On Steinberg Web Page you can find an UniqueID
	Database where you can record your UniqueID, it will check if the ID is already used by an another 
	vendor. You can use CCONST('a','b','c','d') (defined in VST 2.0) to be platform independent to 
	initialize an UniqueID.

	\param iD Plug-in's unique ID

	\note Needs to be called in the plug-in's constructor
*/

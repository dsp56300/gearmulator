//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4       $Date: 2006/10/05 14:23:59 $
//
// Category     : VST 2.x Classes
// Filename     : audioeffectx.cpp
// Created by   : Steinberg Media Technologies
// Description  : Class AudioEffectX extends AudioEffect with new features. You should derive
//                your plug-in from AudioEffectX.
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#include "audioeffectx.h"
#include "aeffeditor.h"

//-------------------------------------------------------------------------------------------------------
/*! hostCanDos strings Plug-in -> Host */
namespace HostCanDos
{
	const char* canDoSendVstEvents = "sendVstEvents"; ///< Host supports send of Vst events to plug-in
	const char* canDoSendVstMidiEvent = "sendVstMidiEvent"; ///< Host supports send of MIDI events to plug-in
	const char* canDoSendVstTimeInfo = "sendVstTimeInfo"; ///< Host supports send of VstTimeInfo to plug-in
	const char* canDoReceiveVstEvents = "receiveVstEvents"; ///< Host can receive Vst events from plug-in
	const char* canDoReceiveVstMidiEvent = "receiveVstMidiEvent"; ///< Host can receive MIDI events from plug-in 
	const char* canDoReportConnectionChanges = "reportConnectionChanges"; ///< Host will indicates the plug-in when something change in plug-in´s routing/connections with #suspend/#resume/#setSpeakerArrangement 
	const char* canDoAcceptIOChanges = "acceptIOChanges"; ///< Host supports #ioChanged ()
	const char* canDoSizeWindow = "sizeWindow"; ///< used by VSTGUI
	const char* canDoOffline = "offline"; ///< Host supports offline feature
	const char* canDoOpenFileSelector = "openFileSelector"; ///< Host supports function #openFileSelector ()
	const char* canDoCloseFileSelector = "closeFileSelector"; ///< Host supports function #closeFileSelector ()
	const char* canDoStartStopProcess = "startStopProcess"; ///< Host supports functions #startProcess () and #stopProcess ()
	const char* canDoShellCategory = "shellCategory"; ///< 'shell' handling via uniqueID. If supported by the Host and the Plug-in has the category #kPlugCategShell
	const char* canDoSendVstMidiEventFlagIsRealtime = "sendVstMidiEventFlagIsRealtime"; ///< Host supports flags for #VstMidiEvent
}

//-------------------------------------------------------------------------------------------------------
/*! plugCanDos strings Host -> Plug-in */
namespace PlugCanDos
{
	const char* canDoSendVstEvents = "sendVstEvents"; ///< plug-in will send Vst events to Host
	const char* canDoSendVstMidiEvent = "sendVstMidiEvent"; ///< plug-in will send MIDI events to Host
	const char* canDoReceiveVstEvents = "receiveVstEvents"; ///< plug-in can receive MIDI events from Host
	const char* canDoReceiveVstMidiEvent = "receiveVstMidiEvent"; ///< plug-in can receive MIDI events from Host 
	const char* canDoReceiveVstTimeInfo = "receiveVstTimeInfo"; ///< plug-in can receive Time info from Host 
	const char* canDoOffline = "offline"; ///< plug-in supports offline functions (#offlineNotify, #offlinePrepare, #offlineRun)
	const char* canDoMidiProgramNames = "midiProgramNames"; ///< plug-in supports function #getMidiProgramName ()
	const char* canDoBypass = "bypass"; ///< plug-in supports function #setBypass ()
}

//-----------------------------------------------------------------------------------------------------------------
// Class AudioEffectX Implementation
//-----------------------------------------------------------------------------------------------------------------
/*!
	\sa AudioEffect()
*/
AudioEffectX::AudioEffectX (audioMasterCallback audioMaster, VstInt32 numPrograms, VstInt32 numParams)
: AudioEffect (audioMaster, numPrograms, numParams)
{}

//-----------------------------------------------------------------------------------------------------------------
VstIntPtr AudioEffectX::dispatcher (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
	VstIntPtr v = 0;
	switch (opcode)
	{
		//---VstEvents----------------------
		case effProcessEvents:
			v = processEvents ((VstEvents*)ptr);
			break;

		//---Parameters and Programs----------------------
		case effCanBeAutomated:
			v = canParameterBeAutomated (index) ? 1 : 0;
			break;
		case effString2Parameter:
			v = string2parameter (index, (char*)ptr) ? 1 : 0;
			break;

		case effGetProgramNameIndexed:
			v = getProgramNameIndexed ((VstInt32)value, index, (char*)ptr) ? 1 : 0;
			break;
	#if !VST_FORCE_DEPRECATED
		case effGetNumProgramCategories:
			v = getNumCategories ();
			break;
		case effCopyProgram:
			v = copyProgram (index) ? 1 : 0;
			break;

		//---Connections, Configuration----------------------
		case effConnectInput:
			inputConnected (index, value ? true : false);
			v = 1;
			break;	
		case effConnectOutput:
			outputConnected (index, value ? true : false);
			v = 1;
			break;	
	#endif // !VST_FORCE_DEPRECATED

		case effGetInputProperties:
			v = getInputProperties (index, (VstPinProperties*)ptr) ? 1 : 0;
			break;
		case effGetOutputProperties:
			v = getOutputProperties (index, (VstPinProperties*)ptr) ? 1 : 0;
			break;
		case effGetPlugCategory:
			v = (VstIntPtr)getPlugCategory ();
			break;

	#if !VST_FORCE_DEPRECATED
		//---Realtime----------------------
		case effGetCurrentPosition:
			v = reportCurrentPosition ();
			break;
			
		case effGetDestinationBuffer:
			v = ToVstPtr<float> (reportDestinationBuffer ());
			break;
	#endif // !VST_FORCE_DEPRECATED

		//---Offline----------------------
		case effOfflineNotify:
			v = offlineNotify ((VstAudioFile*)ptr, (VstInt32)value, index != 0);
			break;
		case effOfflinePrepare:
			v = offlinePrepare ((VstOfflineTask*)ptr, (VstInt32)value);
			break;
		case effOfflineRun:
			v = offlineRun ((VstOfflineTask*)ptr, (VstInt32)value);
			break;

		//---Others----------------------
		case effSetSpeakerArrangement:
			v = setSpeakerArrangement (FromVstPtr<VstSpeakerArrangement> (value), (VstSpeakerArrangement*)ptr) ? 1 : 0;
			break;
		case effProcessVarIo:
			v = processVariableIo ((VstVariableIo*)ptr) ? 1 : 0;
			break;
	#if !VST_FORCE_DEPRECATED
		case effSetBlockSizeAndSampleRate:
			setBlockSizeAndSampleRate ((VstInt32)value, opt);
			v = 1;
			break;
	#endif // !VST_FORCE_DEPRECATED
		case effSetBypass:
			v = setBypass (value ? true : false) ? 1 : 0;
			break;
		case effGetEffectName:
			v = getEffectName ((char*)ptr) ? 1 : 0;
			break;
		case effGetVendorString:
			v = getVendorString ((char*)ptr) ? 1 : 0;
			break;
		case effGetProductString:
			v = getProductString ((char*)ptr) ? 1 : 0;
			break;
		case effGetVendorVersion:
			v = getVendorVersion ();
			break;
		case effVendorSpecific:
			v = vendorSpecific (index, value, ptr, opt);
			break;
		case effCanDo:
			v = canDo ((char*)ptr);
			break;

		case effGetTailSize:
			v = getGetTailSize ();
			break;

	#if !VST_FORCE_DEPRECATED
		case effGetErrorText:
			v = getErrorText ((char*)ptr) ? 1 : 0;
			break;

		case effGetIcon:
			v = ToVstPtr<void> (getIcon ());
			break;

		case effSetViewPosition:
			v = setViewPosition (index, (VstInt32)value) ? 1 : 0;
			break;		

		case effIdle:
			v = fxIdle ();
			break;

		case effKeysRequired:
			v = (keysRequired () ? 0 : 1);	// reversed to keep v1 compatibility
			break;
	#endif // !VST_FORCE_DEPRECATED

		case effGetParameterProperties:
			v = getParameterProperties (index, (VstParameterProperties*)ptr) ? 1 : 0;
			break;

		case effGetVstVersion:
			v = getVstVersion ();
			break;

		//---Others----------------------
	#if VST_2_1_EXTENSIONS
		case effEditKeyDown:
			if (editor)
			{
				VstKeyCode keyCode = {index, (unsigned char)value, (unsigned char)opt};
				v = editor->onKeyDown (keyCode) ? 1 : 0;
			}
			break;

		case effEditKeyUp:
			if (editor)
			{
				VstKeyCode keyCode = {index, (unsigned char)value, (unsigned char)opt};
				v = editor->onKeyUp (keyCode) ? 1 : 0;
			}
			break;

		case effSetEditKnobMode:
			if (editor)
				v = editor->setKnobMode ((VstInt32)value) ? 1 : 0;
			break;

		case effGetMidiProgramName:
			v = getMidiProgramName (index, (MidiProgramName*)ptr);
			break;
		case effGetCurrentMidiProgram:
			v = getCurrentMidiProgram (index, (MidiProgramName*)ptr);
			break;
		case effGetMidiProgramCategory:
			v = getMidiProgramCategory (index, (MidiProgramCategory*)ptr);
			break;
		case effHasMidiProgramsChanged:
			v = hasMidiProgramsChanged (index) ? 1 : 0;
			break;
		case effGetMidiKeyName:
			v = getMidiKeyName (index, (MidiKeyName*)ptr) ? 1 : 0;
			break;
		case effBeginSetProgram:
			v = beginSetProgram () ? 1 : 0;
			break;
		case effEndSetProgram:
			v = endSetProgram () ? 1 : 0;
			break;	
	#endif // VST_2_1_EXTENSIONS

	#if VST_2_3_EXTENSIONS
		case effGetSpeakerArrangement:
			v = getSpeakerArrangement (FromVstPtr<VstSpeakerArrangement*> (value), (VstSpeakerArrangement**)ptr) ? 1 : 0;
			break;

		case effSetTotalSampleToProcess:
			v = setTotalSampleToProcess ((VstInt32)value);
			break;

		case effShellGetNextPlugin:
			v = getNextShellPlugin ((char*)ptr);
			break;

		case effStartProcess:
			v = startProcess ();
			break;
		case effStopProcess:
			v = stopProcess ();
			break;

		case effSetPanLaw:
			v = setPanLaw ((VstInt32)value, opt) ? 1 : 0;
			break;

		case effBeginLoadBank:
			v = beginLoadBank ((VstPatchChunkInfo*)ptr);
			break;
		case effBeginLoadProgram:
			v = beginLoadProgram ((VstPatchChunkInfo*)ptr);
			break;
	#endif // VST_2_3_EXTENSIONS

	#if VST_2_4_EXTENSIONS
		case effSetProcessPrecision :
			v = setProcessPrecision ((VstInt32)value) ? 1 : 0;
			break;

		case effGetNumMidiInputChannels :
			v = getNumMidiInputChannels ();
			break;

		case effGetNumMidiOutputChannels :
			v = getNumMidiOutputChannels ();
			break;
	#endif // VST_2_4_EXTENSIONS

		//---Version 1.0 or unknown-----------------
		default:
			v = AudioEffect::dispatcher (opcode, index, value, ptr, opt);
	}
	return v;
}

//-----------------------------------------------------------------------------------------------------------------
/*! if this effect is a synth or can receive midi events, we call the deprecated wantEvents() as some host rely on it.
*/
void AudioEffectX::resume ()
{
	if (cEffect.flags & effFlagsIsSynth || canDo (const_cast<char*>("receiveVstMidiEvent")) == 1)
		DECLARE_VST_DEPRECATED (wantEvents) ();
}

//-----------------------------------------------------------------------------------------------------------------
void AudioEffectX::DECLARE_VST_DEPRECATED (wantEvents) (VstInt32 filter)
{
	if (audioMaster)
		audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterWantMidi), 0, filter, 0, 0);
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	A plug-in will request time info by calling the function getTimeInfo() which returns a \e #VstTimeInfo
	pointer (or NULL if not implemented by the Host). The mask parameter is composed of the same flags which 
	will be found in the flags field of \e #VstTimeInfo when returned, that is, if you need information about tempo.
	The parameter passed to getTimeInfo() should have the \e #kVstTempoValid flag set. This request and delivery 
	system is important, as a request like this may cause significant calculations at the application's end, which 
	may take a lot of our precious time. This obviously means you should only set those flags that are required to 
	get the information you need. Also please be aware that requesting information does not necessarily mean that 
	that information is provided in return. Check the \e flags field in the \e #VstTimeInfo structure to see if your
	request was actually met.

	\param filter A mask indicating which fields are requested, as some items may require extensive conversions. 
	See the \e flags in #VstTimeInfo
	\return A pointer to a #VstTimeInfo structure or NULL if not implemented by the Host
*/
VstTimeInfo* AudioEffectX::getTimeInfo (VstInt32 filter)
{
	if (audioMaster)
	{
		VstIntPtr ret = audioMaster (&cEffect, audioMasterGetTime, 0, filter, 0, 0);
		return FromVstPtr<VstTimeInfo> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::DECLARE_VST_DEPRECATED (tempoAt) (VstInt32 pos)
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterTempoAt), 0, pos, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
bool AudioEffectX::sendVstEventsToHost (VstEvents* events)
/*!
	Can be called inside processReplacing.

	\param events Fill with VST events 
	\return Returns \e true on success
*/
{
	if (audioMaster)
		return audioMaster (&cEffect, audioMasterProcessEvents, 0, 0, events, 0) == 1;
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::processEvents (VstEvents* events)

	\return	return value is ignored

	\remarks	Events are always related to the current audio block. For each process cycle, processEvents() is called 
			<b>once</b> before a processReplacing() call (if new events are available).
	
	\sa VstEvents, VstMidiEvent
*/

//-----------------------------------------------------------------------------------------------------------------
// Parameters Functions
//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::canParameterBeAutomated (VstInt32 index)
 
	Obviously only useful when the application supports this.

	\param index Index of the parameter
	\return	\true if supported
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::string2parameter (VstInt32 index, char* text)

	Especially useful for plug-ins without user interface. The application can then implement a text edit field for
	the user to set a parameter by entering text.

    \param index Index of the parameter
	\param text A textual description of the parameter's value. A NULL pointer is used to check the capability
	(return true).
	\return \e true on success

	\note Implies setParameter (). text==0 is to be expected to check the capability (returns true)	
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text)

	Allows a Host application to list the plug-in's programs (presets).

	\param category unused in VST 2.4
	\param index Index of the program in a given category, starting with 0.
	\param text A string up to 24 chars.
	\return \e true on success
*/
//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::DECLARE_VST_DEPRECATED (getNumAutomatableParameters) ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetNumAutomatableParameters), 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::DECLARE_VST_DEPRECATED (getParameterQuantization) ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetParameterQuantization), 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
// Configuration/Settings Functions
//-----------------------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------------------
/*!
	The Host could call a suspend() (if the plug-in was enabled (in resume() state)) and then ask for 
	getSpeakerArrangement() and/or check the \e numInputs and \e numOutputs and \e initialDelay and then call a 
	resume().

	\return \e true on success

	\sa setSpeakerArrangement(), getSpeakerArrangement() 
*/
bool AudioEffectX::ioChanged ()
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterIOChanged, 0, 0, 0, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
bool AudioEffectX::DECLARE_VST_DEPRECATED (needIdle) ()
{
	if (audioMaster)
		return (audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterNeedIdle), 0, 0, 0, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\param width The window's width in pixel
	\param height The window's height in pixel
	\return \e true on success
*/
bool AudioEffectX::sizeWindow (VstInt32 width, VstInt32 height)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterSizeWindow, width, height, 0, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
double AudioEffectX::updateSampleRate ()
/*!
	\return The Host's sample rate
*/
{
	if (audioMaster)
	{
		VstIntPtr res = audioMaster (&cEffect, audioMasterGetSampleRate, 0, 0, 0, 0);
		if (res > 0)
			sampleRate = (float)res;
	}
	return sampleRate;
}

//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::updateBlockSize ()
/*!
	\return The Host's block size

	\note Will cause application to call AudioEffect's setSampleRate() to be called (when implemented).
*/
{
	if (audioMaster)
	{
		VstInt32 res = (VstInt32)audioMaster (&cEffect, audioMasterGetBlockSize, 0, 0, 0, 0);
		if (res > 0)
			blockSize = res;
	}
	return blockSize;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return ASIO input latency
	\sa getOutputLatency()
*/
VstInt32 AudioEffectX::getInputLatency ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, audioMasterGetInputLatency, 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	While inputLatency is probably not of concern, outputLatency may be used in conjunction with getTimeInfo().
	\e samplePos of VstTimeInfo is ahead of the 'visual' sequencer play time by the output latency, such that 
	when outputLatency samples have passed by, our processing result becomes audible.

	\return ASIO output latency
	\sa getInputLatency()
*/
VstInt32 AudioEffectX::getOutputLatency ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, audioMasterGetOutputLatency, 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getInputProperties (VstInt32 index, VstPinProperties* properties)

	\param index The index to the input, starting with 0
	\param properties A pointer to a VstPinProperties structure
	\return \e true on success
	\sa getOutputProperties()
	\note Example
	<pre>
	bool MyPlug::getInputProperties (VstInt32 index, VstPinProperties* properties)
	{
		bool returnCode = false;
		if (index < kNumInputs)
		{
			sprintf (properties->label, "My %1d In", index + 1);
			properties->flags = kVstPinIsStereo | kVstPinIsActive;
			returnCode = true;
		}
		return returnCode;
	}
	</pre>
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getOutputProperties (VstInt32 index, VstPinProperties* properties)

	\param index The index to the output, starting with 0
	\param properties A pointer to a VstPinProperties structure
	\return \e true on success
	\sa getInputProperties()
	\note Example 1
	<pre>
	bool MyPlug::getOutputProperties (VstInt32 index, VstPinProperties* properties)
	{
		bool returnCode = false;
		if (index < kNumOutputs)
		{
			sprintf (properties->label, "My %1d Out", index + 1);
			properties->flags = kVstPinIsStereo | kVstPinIsActive;
			returnCode = true;
		}
		return (returnCode);
	}
	</pre>

	\note Example 2 : plug-in with 1 mono, 1 stereo and one 5.1 outputs (kNumOutputs = 9):
	<pre>
	bool MyPlug::getOutputProperties (VstInt32 index, VstPinProperties* properties)
	{
		bool returnCode = false;
		if (index >= 0 && index < kNumOutputs)
		{
			properties->flags = kVstPinIsActive;
			if (index == 0) // mono
			{
				strcpy (properties->label, "Mono Out");
				properties->arrangementType = kSpeakerArrMono;
			}
			else if (index == 1) // stereo (1 -> 2)
			{
				strcpy (properties->label, "Stereo Out");
				properties->flags |= kVstPinIsStereo;
				properties->arrangementType = kSpeakerArrStereo;
			}
			else if (index >= 3) // 5.1 (3 -> 8)
			{
				strcpy (properties->label, "5.1 Out");
				properties->flags |= kVstPinUseSpeaker;
				properties->arrangementType = kSpeakerArr51;
				// for old VST Host < 2.3, make 5.1 to stereo/mono/mono/stereo (L R C Lfe Ls Rs)
				if (index == 3 || index == 7)
					properties->flags |= kVstPinIsStereo;
				if (index == 5)
					strcpy (properties->label, "Center");	
				else if (index == 6)
					strcpy (properties->label, "Lfe");	
				else if (index == 7) // (7 -> 8)
					strcpy (properties->label, "Stereo Back");
			}
			returnCode = true;
		}
		return returnCode;
	}
	</pre>
*/

//-----------------------------------------------------------------------------------------------------------------
AEffect* AudioEffectX::DECLARE_VST_DEPRECATED (getPreviousPlug) (VstInt32 input)
{
	if (audioMaster)
	{
		VstIntPtr ret = audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetPreviousPlug), 0, 0, 0, 0);
		return FromVstPtr<AEffect> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
AEffect* AudioEffectX::DECLARE_VST_DEPRECATED (getNextPlug) (VstInt32 output)
{
	if (audioMaster)
	{
		VstIntPtr ret = audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetNextPlug), 0, 0, 0, 0);
		return FromVstPtr<AEffect> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return Plug-in's category defined in VstPlugCategory
*/
VstPlugCategory AudioEffectX::getPlugCategory ()
{ 
	if (cEffect.flags & effFlagsIsSynth)
		return kPlugCategSynth;
	return kPlugCategUnknown; 
}

//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::DECLARE_VST_DEPRECATED (willProcessReplacing) ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterWillReplaceOrAccumulate), 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	A plug-in is like a black box processing some audio coming in on some inputs (if any) and going out of some 
	outputs (if any). This may be used to do offline or real-time processing, and sometimes it may be desirable to
	know the current context.

	\return #VstProcessLevels in aeffectx.h
 
*/
VstInt32 AudioEffectX::getCurrentProcessLevel ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, audioMasterGetCurrentProcessLevel, 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return #VstAutomationStates in aeffectx.h
*/
VstInt32 AudioEffectX::getAutomationState ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, audioMasterGetAutomationState, 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
void AudioEffectX::DECLARE_VST_DEPRECATED (wantAsyncOperation) (bool state)
{
	if (state)
		cEffect.flags |= DECLARE_VST_DEPRECATED (effFlagsExtIsAsync);
	else
		cEffect.flags &= ~DECLARE_VST_DEPRECATED (effFlagsExtIsAsync);
}

//-----------------------------------------------------------------------------------------------------------------
void AudioEffectX::DECLARE_VST_DEPRECATED (hasExternalBuffer) (bool state)
{
	if (state)
		cEffect.flags |= DECLARE_VST_DEPRECATED (effFlagsExtHasBuffer);
	else
		cEffect.flags &= ~DECLARE_VST_DEPRECATED (effFlagsExtHasBuffer);
}

//-----------------------------------------------------------------------------------------------------------------
// Offline Functions
//-----------------------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------------
bool AudioEffectX::offlineRead (VstOfflineTask* offline, VstOfflineOption option, bool readSource)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterOfflineRead, readSource, option, offline, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
bool AudioEffectX::offlineWrite (VstOfflineTask* offline, VstOfflineOption option)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterOfflineWrite, 0, option, offline, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
bool AudioEffectX::offlineStart (VstAudioFile* audioFiles, VstInt32 numAudioFiles, VstInt32 numNewAudioFiles)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterOfflineStart, numNewAudioFiles, numAudioFiles, audioFiles, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::offlineGetCurrentPass ()
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterOfflineGetCurrentPass, 0, 0, 0, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
VstInt32 AudioEffectX::offlineGetCurrentMetaPass ()
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterOfflineGetCurrentMetaPass, 0, 0, 0, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
// Other
//-----------------------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------------------
void AudioEffectX::DECLARE_VST_DEPRECATED (setOutputSamplerate) (float sampleRate)
{
	if (audioMaster)
		audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterSetOutputSampleRate), 0, 0, 0, sampleRate);
}

//-----------------------------------------------------------------------------------------------------------------
VstSpeakerArrangement* AudioEffectX::DECLARE_VST_DEPRECATED (getInputSpeakerArrangement) ()
{
	if (audioMaster)
	{
		VstIntPtr ret = audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetInputSpeakerArrangement), 0, 0, 0, 0);
		return FromVstPtr<VstSpeakerArrangement> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
VstSpeakerArrangement* AudioEffectX::DECLARE_VST_DEPRECATED (getOutputSpeakerArrangement) ()
{
	if (audioMaster)
	{
		VstIntPtr ret = audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetOutputSpeakerArrangement), 0, 0, 0, 0);
		return FromVstPtr<VstSpeakerArrangement> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\param text  String of maximum 64 char
	\return \e true if supported
*/
bool AudioEffectX::getHostVendorString (char* text)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterGetVendorString, 0, 0, text, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\param text  String of maximum 64 char
	\return \e true if supported
*/
bool AudioEffectX::getHostProductString (char* text)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterGetProductString, 0, 0, text, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return Host vendor version
*/
VstInt32 AudioEffectX::getHostVendorVersion ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, audioMasterGetVendorVersion, 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
VstIntPtr AudioEffectX::hostVendorSpecific (VstInt32 lArg1, VstIntPtr lArg2, void* ptrArg, float floatArg)
{
	if (audioMaster)
		return audioMaster (&cEffect, audioMasterVendorSpecific, lArg1, lArg2, ptrArg, floatArg);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	Asks Host if it implements the feature text. A plug-in cannot assume a 2.x feature is available from the Host. 
	Use this method to ascertain the environment in which the plug-in finds itself. Ignoring this inquiry methods and 
	trying to access a 2.x feature in a 1.0 Host will mean your plug-in or Host application will break. It is not 
	the end-users job to pick and choose which plug-ins can be supported by which Host.

	\param text A string from #hostCanDos
	\return
	- 0 : don't know (default)
	- 1 : yes
	- -1: no
*/
VstInt32 AudioEffectX::canHostDo (char* text)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterCanDo, 0, 0, text, 0) != 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	Tells the Host that the plug-in is an instrument, i.e. that it will call wantEvents().
	
	\param state
	- true: is an instrument (default)
	- false: is a simple audio effect
*/
void AudioEffectX::isSynth (bool state)
{
	if (state)
		cEffect.flags |= effFlagsIsSynth;
	else
		cEffect.flags &= ~effFlagsIsSynth;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	Enables Host to omit processReplacing() when no data is present on any input.
*/
void AudioEffectX::noTail (bool state)
{
	if (state)
		cEffect.flags |= effFlagsNoSoundInStop;
	else
		cEffect.flags &= ~effFlagsNoSoundInStop;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return #VstHostLanguage in aeffectx.h
*/
VstInt32 AudioEffectX::getHostLanguage ()
{
	if (audioMaster)
		return (VstInt32)audioMaster (&cEffect, audioMasterGetLanguage, 0, 0, 0, 0);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
void* AudioEffectX::DECLARE_VST_DEPRECATED (openWindow) (DECLARE_VST_DEPRECATED (VstWindow)* window)
{
	if (audioMaster)
	{
		VstIntPtr ret = audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterOpenWindow), 0, 0, window, 0);
		return FromVstPtr<void> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
bool AudioEffectX::DECLARE_VST_DEPRECATED (closeWindow) (DECLARE_VST_DEPRECATED (VstWindow)* window)
{
	if (audioMaster)
		return (audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterCloseWindow), 0, 0, window, 0) != 0);
	return false;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return FSSpec on MAC, else char*
*/
void* AudioEffectX::getDirectory ()
{
	if (audioMaster)
	{
		VstIntPtr ret = (audioMaster (&cEffect, audioMasterGetDirectory, 0, 0, 0, 0));
		return FromVstPtr<void> (ret);
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\return \e true if supported
*/
bool AudioEffectX::updateDisplay ()
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterUpdateDisplay, 0, 0, 0, 0)) ? true : false;
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::processVariableIo (VstVariableIo* varIo)

	If called with \e varIo NULL, returning \e true indicates that this call is supported by the plug-in.
	Host will use processReplacing otherwise. The Host should call setTotalSampleToProcess before starting the processIO
	to inform the plug-in about how many samples will be processed in total. The Host should provide an output buffer at least 5 times bigger than input buffer.

	\param varIo
	\return \true on success
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::setSpeakerArrangement (VstSpeakerArrangement* pluginInput, VstSpeakerArrangement* pluginOutput)

	Set the plug-in's speaker arrangements. If a (VST >= 2.3) plug-in returns \e true, it means that it accepts this IO 
	arrangement. The Host doesn't need to ask for getSpeakerArrangement(). If the plug-in returns \e false it means that it 
	doesn't accept this arrangement, the Host should then ask for getSpeakerArrangement() and then can (optional)
	recall setSpeakerArrangement().

	\param pluginInput A pointer to the input's #VstSpeakerArrangement structure.
	\param pluginOutput A pointer to the output's #VstSpeakerArrangement structure.
	\return \e true on success

	\note setSpeakerArrangement() and getSpeakerArrangement() are always called in suspended state. 
	(like setSampleRate() or setBlockSize()).

	\sa getSpeakerArrangement()
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput)
	
	\param pluginInput A pointer to the input's #VstSpeakerArrangement structure.
	\param pluginOutput A pointer to the output's #VstSpeakerArrangement structure.
	\return \e true on success

	\note setSpeakerArrangement() and getSpeakerArrangement() are always called in suspended state. 
	(like setSampleRate() or setBlockSize()).\n
	<pre>Here an example code to show how the host uses getSpeakerArrangement()
	VstSpeakerArrangement *plugInputVstArr = 0;
	VstSpeakerArrangement *plugOutputVstArr = 0;
	if (getFormatVersion () >= 2300 && #getSpeakerArrangement (&plugInputVstArr, &plugOutputVstArr))
		....
	</pre>	

	\sa setSpeakerArrangement()
*/
//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::setBypass (bool onOff)

	process still called (if Supported) although the plug-in was bypassed. Some plugs need to stay 'alive' even 
	when bypassed. An example is a surround decoder which has more inputs than outputs and must maintain some 
	reasonable signal distribution even when being bypassed. A CanDo 'bypass' allows to ask the plug-in if it 
	supports soft bypass or not. 

	\note This bypass feature could be automated by the Host (this means avoid to much CPU requirement in this call)
	\note If the plug-in supports SoftBypass and it has a latency (initialDelay), in Bypassed state the plug-in has to used
	the same latency value. 
	
	\param onOff
	\return
	- true: supports SoftBypass, process will be called, the plug-in should compensate its latency, and copy inputs to outputs  
	- false: doesn't support SoftBypass, process will not be called, the Host should bypass the process call

	\sa processReplacing()
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getEffectName (char* name)

	\param name A string up to 32 chars
	\return \e true on success
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getVendorString (char* text)

	\param text A string up to 64 chars
	\return \e true on success
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getProductString (char* text)

	\param text A string up to 64 chars
	\return \e true on success
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getVendorVersion ()

	\return The version of the plug-in

	\note This should be upported
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::canDo (char* text)

	Report what the plug-in is able to do. In general you can but don't have to report whatever you support or not 
	support via canDo. Some application functionality may require some specific reply, but in that case you will 
	probably know. Best is to report whatever you know for sure. A Host application cannot make assumptions about
	the presence of the new 2.x features of a plug-in. Ignoring this inquiry methods and trying to access a 2.x 
	feature from a 1.0 plug, or vice versa, will mean the plug-in or Host application will break. It is not the
	end-users job to pick and choose which plug-ins can be supported by which Host.

	\param text A string from #plugCanDos
	\return
	- 0: don't know (default)
	- 1: yes
	- -1: no

	\note This should be supported.
*/

//----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::canDo (char* text)

	\param text A string from #plugCanDos
	\return
	- 0: don't know (default). 
	- 1: yes.
	- -1: no
*/

//----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getParameterProperties (VstInt32 index, VstParameterProperties* p)

	\param index Index of the parameter
	\param p Pointer to #VstParameterProperties
	\return Return \e true on success
*/

//----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getVstVersion ()
	\return  
	- 2xxx : the last VST 2.x plug-in version (by default) 
	- 0 : older versions
 
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32  AudioEffectX::getMidiProgramName (VstInt32 channel, MidiProgramName* midiProgramName)
	Ask plug-in if MidiPrograms are used and if so, query for names, numbers 
	(ProgramChange-Number + BankSelect-Number), categories and keynames of each 
	MIDI Program, on each MIDI-channel. If this function is called, your plug-in has to read 
	MidiProgramName::thisProgramIndex, fill out the other fields with the information 
	assigned to a certain MIDI Program and return the number of available MIDI Programs on 
	that MIDI Channel. 
	
	\note plug-in canDo "midiProgramNames". No effect, if 0 is returned. 
	
	\warning don't mix concepts: the MIDI Programs are totally independent from all other 
	programs present in VST. The main difference is, that there are upto 16 simultaneous 
	active MIDI Programs (one per channel), while there can be only one active "VST"-Program. 
	(You should see the "VST"-Program as the one single main global program, which contains 
	the entire current state of the plug-in.) This function can be called in any sequence. 
	
	\param channel MidiChannel: 0-15
	\param midiProgramName Points to \e #MidiProgramName struct
	\return Number of available MIDI Programs on that \e channel
	- number of used programIndexes
	- 0 if no MidiProgramNames supported

	\note Example : plug-in has 3 MidiPrograms on MidiChannel 0.
	<pre>
	Host calls #getMidiProgramName with idx = 0 and MidiProgramName::thisProgramIndex = 0.
	Plug fills out:
	MidiProgramName::name[64] = "Program A"
	MidiProgramName::midiProgram = 0
	MidiProgramName::midiBankMsb = -1
	MidiProgramName::midiBankLsb = -1
	MidiProgramName::parentCategoryIndex = -1
	MidiProgramName::flags = 0 (if plug isn't "Omni").
	Plug returns 3.
	Host calls #getMidiProgramName with idx = 0 and MidiProgramName::thisProgramIndex = 1.
	Plug fills out:
	MidiProgramName::name[64] = "Program B"
	MidiProgramName::midiProgram = 1
	MidiProgramName::midiBankMsb = -1
	MidiProgramName::midiBankLsb = -1
	MidiProgramName::parentCategoryIndex = -1
	MidiProgramName::flags = 0 (if plug isn't "Omni").
	Plug returns 3.
	Host calls #getMidiProgramName with idx = 0 and MidiProgramName::thisProgramIndex = 2.
	Plug fills out:
	MidiProgramName::name[64] = "Program C"
	MidiProgramName::midiProgram = 2
	MidiProgramName::midiBankMsb = -1
	MidiProgramName::midiBankLsb = -1
	MidiProgramName::parentCategoryIndex = -1
	MidiProgramName::flags = 0 (if plug isn't "Omni").
	Plug returns 3.
	</pre>
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getCurrentMidiProgram (VstInt32 channel, MidiProgramName* currentProgram)
	
	\param channel
	\param currentProgram
	\return
	- programIndex of the current program
	- -1 if not supported
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getMidiProgramCategory (VstInt32 channel, MidiProgramCategory* category)
	
	\param channel
	\param category
	\return
	- number of used categoryIndexes. 
	- 0 if no #MidiProgramCategory supported/used.
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::hasMidiProgramsChanged (VstInt32 channel)
	
	Ask plug-in for the currently active program on a certain MIDI Channel. Just like 
	getMidiProgramName(), but MidiProgramName::thisProgramIndex has to be filled out with
	the currently active MIDI Program-index, which also has to be returned. 
	
	\param channel
	\return
	- true: if the #MidiProgramNames, #MidiKeyNames or #MidiControllerNames had changed on 
	this channel
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::getMidiKeyName (VstInt32 channel, MidiKeyName* keyName)
	
	\param channel
	\param keyName If keyName is "" the standard name of the key will be displayed
	\return Return \e false if no #MidiKeyNames defined for 'thisProgramIndex'
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::beginSetProgram ()
	
	\return
	- true: the plug-in took the notification into account
	- false: it did not...

	\sa endSetProgram()
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::endSetProgram ()
	
	\return
	- true: the plug-in took the notification into account
	- false: it did not...

	\sa beginSetProgram()
*/

#if VST_2_1_EXTENSIONS
//-----------------------------------------------------------------------------------------------------------------
/*!
	It tells the Host that if it needs to, it has to record automation data for this control.
	
	\param index Index of the parameter
	\return Returns \e true on success

	\sa endEdit()
*/
bool AudioEffectX::beginEdit (VstInt32 index)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterBeginEdit, index, 0, 0, 0)) ? true : false;
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	It notifies the Host that this control is no longer moved by the mouse.
	
	\param index Index of the parameter
	\return Returns \e true on success

	\sa beginEdit()
*/
bool AudioEffectX::endEdit (VstInt32 index)
{
	if (audioMaster)
		return (audioMaster (&cEffect, audioMasterEndEdit, index, 0, 0, 0)) ? true : false;
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\param ptr
	\return Returns \e true on success

	\sa closeFileSelector()
*/
bool AudioEffectX::openFileSelector (VstFileSelect* ptr)
{
	if (audioMaster && ptr)
		return (audioMaster (&cEffect, audioMasterOpenFileSelector, 0, 0, ptr, 0)) ? true : false;
	return 0;
}
#endif // VST_2_1_EXTENSIONS

#if VST_2_2_EXTENSIONS
//-----------------------------------------------------------------------------------------------------------------
/*!
	\param ptr
	\return Returns \e true on success

	\sa openFileSelector()
*/
bool AudioEffectX::closeFileSelector (VstFileSelect* ptr)
{
	if (audioMaster && ptr)
		return (audioMaster (&cEffect, audioMasterCloseFileSelector, 0, 0, ptr, 0)) ? true : false;
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------
/*!	
	It indicates how many samples will be processed.

	\param nativePath
	\return Returns \e true on success 

	\sa getChunk(), setChunk()
*/
bool AudioEffectX::DECLARE_VST_DEPRECATED (getChunkFile) (void* nativePath)
{
	if (audioMaster && nativePath)
		return (audioMaster (&cEffect, DECLARE_VST_DEPRECATED (audioMasterGetChunkFile), 0, 0, nativePath, 0)) ? true : false;
	return 0;
}
#endif // VST_2_2_EXTENSIONS

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::setTotalSampleToProcess (VstInt32 value)
	
	It indicates how many samples will be processed in total.

	\param value Number of samples to process
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getNextShellPlugin (char* name) { return 0; }
	
	\param name Points to a char buffer of size 64, which is to be filled with the name of the
	plug-in including the terminating zero
	\return Return the next plug-in's uniqueID
	\note Example of Implementation 
<pre>
	//---From the Host side : if found plugin is a Shell category-----------
	if (effect->getCategory () == kPlugCategShell)
	{ 
		// scan shell for subplugins
		char tempName[64] = {0}; 
		VstInt32 plugUniqueID = 0;
		while ((plugUniqueID = effect->dispatchEffect (effShellGetNextPlugin, 0, 0, tempName)) != 0)
		{ 
			// subplug needs a name 
			if (tempName[0] != 0)
			{
				...do what you want with this tempName and plugUniqueID
			}
		}
	}
	//---From the Host side : Intanciate a subplugin of a shell plugin---
	// retreive the uniqueID of this subplugin the host wants to load
	// set it to the host currentID
	currentID = subplugInfo->uniqueID;
	// call the its shell plugin (main function)
	main ();
	// the shell plugin will ask for the currentUniqueID
	// and should return the chosen subplugin
	...
	//---From the plugin-Shell Side: for enumeration of subplugins---------
	category = kPlugCategShell;
	->can ask the host if "shellCategory" is supported
	// at start (instanciation) reset the index for the getNextShellPlugin call.
	myPluginShell::index = 0;
	// implementation of getNextShellPlugin (char* name);
	VstInt32 myPluginShell::getNextShellPlugin (char* name)
	{
		strcpy (name, MyNameTable[index]);
		return MyUniqueIDTable[index++];
	}
	....
	//---From the plugin-Shell Side: when instanciation-----
	VstInt32 uniqueID = host->getCurrentUniqueID ();
	if (uniqueID == 0) // the host instanciates the shell
	{}
	else // host try to instanciate one of my subplugin...identified by the uniqueID
	{}
</pre>
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::setPanLaw (VstInt32 type, float val) 
	
	\param type
	\param val
	
	\return Returns \e true on success 
	
	\note Gain: for Linear : [1.0 => 0dB PanLaw], [~0.58 => -4.5dB], [0.5 => -6.02dB]
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::beginLoadBank (VstPatchChunkInfo* ptr)
	
	\param ptr
	\return
	- -1: if the Bank cannot be loaded, 
	- 1: if it can be loaded 
	- 0: else (for compatibility)

	\sa beginLoadProgram()

*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::beginLoadProgram (VstPatchChunkInfo* ptr)

	\param ptr
	\return
	- -1: if the Program cannot be loaded, 
	- 1: it can be loaded else,
	- 0: else (for compatibility)

	\sa beginLoadBank()
*/

//-----------------------------------------------------------------------------------------------------------------
// Speaker Arrangement Helpers
//-----------------------------------------------------------------------------------------------------------------

#if VST_2_3_EXTENSIONS
//-----------------------------------------------------------------------------------------------------------------
/*!
	\param arrangement Pointer to a \e #VstSpeakerArrangement structure
	\param nbChannels Number of Channels
	\return Returns \e true on success

	\sa deallocateArrangement(), copySpeaker(), matchArrangement()
*/
bool AudioEffectX::allocateArrangement (VstSpeakerArrangement** arrangement, VstInt32 nbChannels)
{
	if (*arrangement) 
	{ 
		char *ptr = (char*)(*arrangement); 
		delete [] ptr;
	}

	VstInt32 size = 2 * sizeof (VstInt32) + nbChannels * sizeof (VstSpeakerProperties);
	char* ptr = new char[size];
	if (!ptr)
		return false;

	memset (ptr, 0, size);
	*arrangement = (VstSpeakerArrangement*)ptr;
	(*arrangement)->numChannels = nbChannels;
	return true;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\param arrangement Pointer to a \e #VstSpeakerArrangement structure
	\return Returns \e true on success

	\sa allocateArrangement(), copySpeaker(), matchArrangement()
*/
bool AudioEffectX::deallocateArrangement (VstSpeakerArrangement** arrangement)
{
	if (*arrangement) 
	{ 
		char *ptr = (char*)(*arrangement); 
		delete [] ptr; 
		*arrangement = 0;
	}
	return true;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	Feed the \e to speaker properties with the same values than \e from 's ones.
	It is assumed here that \e to exists yet, ie this function won't
	allocate memory for the speaker (this will prevent from having
	a difference between an Arrangement's number of channels and
	 its actual speakers...)
	 
	 \param to
	 \param from
	 \return Returns \e true on success

	 \sa allocateArrangement(), deallocateArrangement(), matchArrangement()
*/
bool AudioEffectX::copySpeaker (VstSpeakerProperties* to, VstSpeakerProperties* from)
{
	if ((from == NULL) || (to == NULL))
		return false;
	
	vst_strncpy (to->name, from->name, 63);
	to->type      = from->type;
	to->azimuth   = from->azimuth;
	to->elevation = from->elevation;
	to->radius    = from->radius;
	to->reserved  = from->reserved;
	memcpy (to->future, from->future, 28);
	
	return true;
}

//-----------------------------------------------------------------------------------------------------------------
/*!
	\e to is deleted, then created and initialized with the same values as \e from (must exist!).
	It's notably useful when setSpeakerArrangement() is called by the Host. 
	
	\param to
	\param from
	\return Returns \e true on success

	\sa allocateArrangement(), deallocateArrangement(), copySpeaker()
*/

bool AudioEffectX::matchArrangement (VstSpeakerArrangement** to, VstSpeakerArrangement* from)
{
	if (from == NULL)
		return false;

	if ((!deallocateArrangement (to)) || (!allocateArrangement (to, from->numChannels)))
		return false;
	
	(*to)->type = from->type;
	for (VstInt32 i = 0; i < (*to)->numChannels; i++)
	{
		if (!copySpeaker (&((*to)->speakers[i]), &(from->speakers[i])))
			return false;
	}

	return true;
}
#endif // VST_2_3_EXTENSIONS

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn bool AudioEffectX::setProcessPrecision (VstInt32 precision)

	Is called in suspended state, similar to #setBlockSize. Default (if not called) is single precision float.

	\param precision kVstProcessPrecision32 or kVstProcessPrecision64
	\return Returns \e true on success
	\sa VstProcessPrecision
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getNumMidiInputChannels ()

	Called by the host application to determine how many MIDI input channels are actually used by a plugin
	e.g. to hide unused channels from the user. 
	For compatibility with VST 2.3 and below, the default return value 0 means 'not implemented' -
	in this case the host assumes 16 MIDI channels to be present (or none at all).

	\return Number of MIDI input channels: 1-15, otherwise: 16 or no MIDI channels at all (0)

	\note The VST 2.x protocol is limited to a maximum of 16 MIDI channels as defined by the MIDI Standard. This might change in future revisions of the API.
	
	\sa
	getNumMidiOutputChannels() @n
	PlugCanDos::canDoReceiveVstMidiEvent
*/

//-----------------------------------------------------------------------------------------------------------------
/*!
	\fn VstInt32 AudioEffectX::getNumMidiOutputChannels ()

	Called by the host application to determine how many MIDI output channels are actually used by a plugin
	e.g. to hide unused channels from the user. 
	For compatibility with VST 2.3 and below, the default return value 0 means 'not implemented' -
	in this case the host assumes 16 MIDI channels to be present (or none at all).

	\return Number of MIDI output channels: 1-15, otherwise: 16 or no MIDI channels at all (0)

	\note The VST 2.x protocol is limited to a maximum of 16 MIDI channels as defined by the MIDI Standard. This might change in future revisions of the API.

	\sa
	getNumMidiInputChannels() @n
	PlugCanDos::canDoSendVstMidiEvent
*/

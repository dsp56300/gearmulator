//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4       $Date: 2006/06/20 12:42:46 $
//
// Category     : VST 2.x Classes
// Filename     : audioeffectx.h
// Created by   : Steinberg Media Technologies
// Description  : Class AudioEffectX extends AudioEffect with new features. You should derive
//                your plug-in from AudioEffectX.
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __audioeffectx__
#define __audioeffectx__

#include "audioeffect.h"	// Version 1.0 base class AudioEffect

#include "pluginterfaces/vst2.x/aeffectx.h"	// Version 2.x 'C' Extensions and Structures

//-------------------------------------------------------------------------------------------------------
/** Extended VST Effect Class (VST 2.x). */
//-------------------------------------------------------------------------------------------------------
class AudioEffectX : public AudioEffect
{
public:
	AudioEffectX (audioMasterCallback audioMaster, VstInt32 numPrograms, VstInt32 numParams); ///< Create an \e AudioEffectX object

//-------------------------------------------------------------------------------------------------------
/// \name Parameters
//-------------------------------------------------------------------------------------------------------
//@{
	virtual bool canParameterBeAutomated (VstInt32 index) { return true; }			///< Indicates if a parameter can be automated
	virtual bool string2parameter (VstInt32 index, char* text) { return false; }	///< Convert a string representation to a parameter value
	virtual bool getParameterProperties (VstInt32 index, VstParameterProperties* p) { return false; } ///< Return parameter properties

#if VST_2_1_EXTENSIONS
	virtual bool beginEdit (VstInt32 index);  ///< To be called before #setParameterAutomated (on Mouse Down). This will be used by the Host for specific Automation Recording.
	virtual bool endEdit (VstInt32 index);    ///< To be called after #setParameterAutomated (on Mouse Up)
#endif // VST_2_1_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Programs and Persistence
//-------------------------------------------------------------------------------------------------------
//@{
	virtual bool getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text) { return false; } ///< Fill \e text with name of program \e index (\e category deprecated in VST 2.4)

#if VST_2_1_EXTENSIONS
	virtual bool beginSetProgram () { return false; }	///< Called before a program is loaded
	virtual bool endSetProgram () { return false; }		///< Called after a program was loaded
#endif // VST_2_1_EXTENSIONS

#if VST_2_3_EXTENSIONS
	virtual VstInt32 beginLoadBank (VstPatchChunkInfo* ptr) { return 0; }		///< Called before a Bank is loaded.
	virtual VstInt32 beginLoadProgram (VstPatchChunkInfo* ptr) { return 0; }	///< Called before a Program is loaded. (called before #beginSetProgram).
#endif // VST_2_3_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Connections and Configuration
//-------------------------------------------------------------------------------------------------------
//@{
	virtual bool ioChanged ();				///< Tell Host numInputs and/or numOutputs and/or initialDelay (and/or numParameters: to be avoid) have changed

	virtual double updateSampleRate ();		///< Returns sample rate from Host (may issue setSampleRate())
	virtual VstInt32 updateBlockSize ();	///< Returns block size from Host (may issue getBlockSize())
	virtual VstInt32 getInputLatency ();	///< Returns the Audio (maybe ASIO) input latency values
	virtual VstInt32 getOutputLatency ();	///< Returns the Audio (maybe ASIO) output latency values

	virtual bool getInputProperties (VstInt32 index, VstPinProperties* properties) { return false; } ///< Return the \e properties of output \e index
	virtual bool getOutputProperties (VstInt32 index, VstPinProperties* properties) { return false; }///< Return the \e properties of input \e index

	virtual bool setSpeakerArrangement (VstSpeakerArrangement* pluginInput, VstSpeakerArrangement* pluginOutput) { return false; } ///< Set the plug-in's speaker arrangements
	virtual bool getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput) { *pluginInput = 0; *pluginOutput = 0; return false; } ///< Return the plug-in's speaker arrangements
	virtual bool setBypass (bool onOff) { return false; }				///< For 'soft-bypass' (this could be automated (in Audio Thread) that why you could NOT call iochanged (if needed) in this function, do it in fxidle).

#if VST_2_3_EXTENSIONS
	virtual bool setPanLaw (VstInt32 type, float val) { return false; }	///< Set the Panning Law used by the Host @see VstPanLawType.
#endif // VST_2_3_EXTENSIONS

#if VST_2_4_EXTENSIONS
	virtual bool setProcessPrecision (VstInt32 precision) { return false; }	///< Set floating-point precision used for processing (32 or 64 bit)

	virtual VstInt32 getNumMidiInputChannels ()  { return 0; }				///< Returns number of MIDI input channels used [0, 16]
	virtual VstInt32 getNumMidiOutputChannels () { return 0; }				///< Returns number of MIDI output channels used [0, 16]
#endif // VST_2_4_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Realtime
//-------------------------------------------------------------------------------------------------------
//@{
	virtual VstTimeInfo* getTimeInfo (VstInt32 filter);	///< Get time information from Host
	virtual VstInt32 getCurrentProcessLevel ();			///< Returns the Host's process level
	virtual VstInt32 getAutomationState ();				///< Returns the Host's automation state

	virtual VstInt32 processEvents (VstEvents* events) { return 0; }	///< Called when new MIDI events come in
	bool sendVstEventsToHost (VstEvents* events);						///< Send MIDI events back to Host application

#if VST_2_3_EXTENSIONS
	virtual VstInt32 startProcess () { return 0; }		///< Called one time before the start of process call. This indicates that the process call will be interrupted (due to Host reconfiguration or bypass state when the plug-in doesn't support softBypass)
	virtual VstInt32 stopProcess () { return 0;}		///< Called after the stop of process call
#endif // VST_2_3_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Variable I/O (Offline)
//-------------------------------------------------------------------------------------------------------
//@{
	virtual bool processVariableIo (VstVariableIo* varIo) { return false; }		///< Used for variable I/O processing (offline processing like timestreching)

#if VST_2_3_EXTENSIONS
	virtual VstInt32 setTotalSampleToProcess (VstInt32 value) {	return value; }	///< Called in offline mode before process() or processVariableIo ()
#endif // VST_2_3_EXTENSIONS
	//@}

//-------------------------------------------------------------------------------------------------------
/// \name Host Properties
//-------------------------------------------------------------------------------------------------------
//@{
	virtual bool getHostVendorString (char* text);	///< Fills \e text with a string identifying the vendor
	virtual bool getHostProductString (char* text);	///< Fills \e text with a string with product name
	virtual VstInt32 getHostVendorVersion ();		///< Returns vendor-specific version (for example 3200 for Nuendo 3.2)
	virtual VstIntPtr hostVendorSpecific (VstInt32 lArg1, VstIntPtr lArg2, void* ptrArg, float floatArg);	///< No specific definition
	virtual VstInt32 canHostDo (char* text);		///< Reports what the Host is able to do (#hostCanDos in audioeffectx.cpp)
	virtual VstInt32 getHostLanguage ();			///< Returns the Host's language (#VstHostLanguage)
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Plug-in Properties
//-------------------------------------------------------------------------------------------------------
//@{
	virtual void isSynth (bool state = true);		///< Set if plug-in is a synth
	virtual void noTail (bool state = true);		///< Plug-in won't produce output signals while there is no input
	virtual VstInt32 getGetTailSize () { return 0; }///< Returns tail size; 0 is default (return 1 for 'no tail'), used in offline processing too
	virtual void* getDirectory ();					///< Returns the plug-in's directory
	virtual bool getEffectName (char* name) { return false; }	///< Fill \e text with a string identifying the effect
	virtual bool getVendorString (char* text) { return false; }	///< Fill \e text with a string identifying the vendor
	virtual bool getProductString (char* text) { return false; }///< Fill \e text with a string identifying the product name
	virtual VstInt32 getVendorVersion () { return 0; }			///< Return vendor-specific version
	virtual VstIntPtr vendorSpecific (VstInt32 lArg, VstIntPtr lArg2, void* ptrArg, float floatArg) { return 0; } ///< No definition, vendor specific handling
	virtual VstInt32 canDo (char* text) { return 0; }			///< Reports what the plug-in is able to do (#plugCanDos in audioeffectx.cpp)
	virtual VstInt32 getVstVersion () { return kVstVersion; }	///< Returns the current VST Version (#kVstVersion)
	virtual VstPlugCategory getPlugCategory ();		///< Specify a category that fits the plug (#VstPlugCategory)
//@}

//-------------------------------------------------------------------------------------------------------
/// \name MIDI Channel Programs
//-------------------------------------------------------------------------------------------------------
//@{
#if VST_2_1_EXTENSIONS
	virtual VstInt32 getMidiProgramName (VstInt32 channel, MidiProgramName* midiProgramName) { return 0; }		///< Fill \e midiProgramName with information for 'thisProgramIndex'.
	virtual VstInt32 getCurrentMidiProgram (VstInt32 channel, MidiProgramName* currentProgram) { return -1; }	///< Fill \e currentProgram with information for the current MIDI program.
	virtual VstInt32 getMidiProgramCategory (VstInt32 channel, MidiProgramCategory* category) { return 0; }		///< Fill \e category with information for 'thisCategoryIndex'.
	virtual bool hasMidiProgramsChanged (VstInt32 channel) { return false; } ///< Return true if the #MidiProgramNames, #MidiKeyNames or #MidiControllerNames had changed on this MIDI channel.
	virtual bool getMidiKeyName (VstInt32 channel, MidiKeyName* keyName) { return false; } ///< Fill \e keyName with information for 'thisProgramIndex' and 'thisKeyNumber'
#endif // VST_2_1_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Others
//-------------------------------------------------------------------------------------------------------
//@{
	virtual bool updateDisplay (); ///< Something has changed in plug-in, request an update display like program (MIDI too) and parameters list in Host
	virtual bool sizeWindow (VstInt32 width, VstInt32 height);	///< Requests to resize the editor window

#if VST_2_1_EXTENSIONS
	virtual bool openFileSelector (VstFileSelect* ptr);			///< Open a Host File selector (see aeffectx.h for #VstFileSelect definition)
#endif // VST_2_1_EXTENSIONS

#if VST_2_2_EXTENSIONS
	virtual bool closeFileSelector (VstFileSelect* ptr);		///< Close the Host File selector which was opened by #openFileSelector
#endif // VST_2_2_EXTENSIONS

#if VST_2_3_EXTENSIONS
	virtual VstInt32 getNextShellPlugin (char* name) { return 0; }	 ///< This opcode is only called, if the plug-in is of type #kPlugCategShell, in order to extract all included sub-plugin´s names.
#endif // VST_2_3_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
/// \name Tools
//-------------------------------------------------------------------------------------------------------
//@{
#if VST_2_3_EXTENSIONS
	virtual bool allocateArrangement (VstSpeakerArrangement** arrangement, VstInt32 nbChannels);///< Allocate memory for a #VstSpeakerArrangement
	virtual bool deallocateArrangement (VstSpeakerArrangement** arrangement);			///< Delete/free memory for an allocated speaker arrangement
	virtual bool copySpeaker (VstSpeakerProperties* to, VstSpeakerProperties* from);	///< Copy properties \e from to \e to
	virtual bool matchArrangement (VstSpeakerArrangement** to, VstSpeakerArrangement* from);	///< "to" is deleted, then created and initialized with the same values as "from" ones ("from" must exist).
#endif // VST_2_3_EXTENSIONS
//@}

//-------------------------------------------------------------------------------------------------------
// Offline
//-------------------------------------------------------------------------------------------------------
/// @cond ignore
	virtual bool offlineRead (VstOfflineTask* offline, VstOfflineOption option, bool readSource = true);
	virtual bool offlineWrite (VstOfflineTask* offline, VstOfflineOption option);
	virtual bool offlineStart (VstAudioFile* ptr, VstInt32 numAudioFiles, VstInt32 numNewAudioFiles);
	virtual VstInt32 offlineGetCurrentPass ();
	virtual VstInt32 offlineGetCurrentMetaPass ();
	virtual bool offlineNotify (VstAudioFile* ptr, VstInt32 numAudioFiles, bool start) { return false; }
	virtual bool offlinePrepare (VstOfflineTask* offline, VstInt32 count) { return false; }
	virtual bool offlineRun (VstOfflineTask* offline, VstInt32 count) { return false; }
	virtual VstInt32 offlineGetNumPasses () { return 0; }
	virtual VstInt32 offlineGetNumMetaPasses () { return 0; }

//-------------------------------------------------------------------------------------------------------
// AudioEffect overrides
//-------------------------------------------------------------------------------------------------------
	virtual VstIntPtr dispatcher (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
	virtual void resume ();

//-------------------------------------------------------------------------------------------------------
// Deprecated methods
//-------------------------------------------------------------------------------------------------------
	virtual void DECLARE_VST_DEPRECATED (wantEvents) (VstInt32 filter = 1);
	virtual VstInt32 DECLARE_VST_DEPRECATED (tempoAt) (VstInt32 pos);
	virtual VstInt32 DECLARE_VST_DEPRECATED (getNumAutomatableParameters) ();
	virtual VstInt32 DECLARE_VST_DEPRECATED (getParameterQuantization) ();
	virtual VstInt32 DECLARE_VST_DEPRECATED (getNumCategories) () { return 1L; }
	virtual bool DECLARE_VST_DEPRECATED (copyProgram) (VstInt32 destination) { return false; }
	virtual bool DECLARE_VST_DEPRECATED (needIdle) ();
	virtual AEffect* DECLARE_VST_DEPRECATED (getPreviousPlug) (VstInt32 input);
	virtual AEffect* DECLARE_VST_DEPRECATED (getNextPlug) (VstInt32 output);
	virtual void DECLARE_VST_DEPRECATED (inputConnected) (VstInt32 index, bool state) {}
	virtual void DECLARE_VST_DEPRECATED (outputConnected) (VstInt32 index, bool state) {}
	virtual VstInt32 DECLARE_VST_DEPRECATED (willProcessReplacing) ();
	virtual void DECLARE_VST_DEPRECATED (wantAsyncOperation) (bool state = true);
	virtual void DECLARE_VST_DEPRECATED (hasExternalBuffer) (bool state = true);
	virtual VstInt32 DECLARE_VST_DEPRECATED (reportCurrentPosition) () { return 0; }
	virtual float* DECLARE_VST_DEPRECATED (reportDestinationBuffer) () { return 0; }
	virtual void DECLARE_VST_DEPRECATED (setOutputSamplerate) (float samplerate);
	virtual VstSpeakerArrangement* DECLARE_VST_DEPRECATED (getInputSpeakerArrangement) ();
	virtual VstSpeakerArrangement* DECLARE_VST_DEPRECATED (getOutputSpeakerArrangement) ();
	virtual void* DECLARE_VST_DEPRECATED (openWindow) (DECLARE_VST_DEPRECATED (VstWindow)*);
	virtual bool DECLARE_VST_DEPRECATED (closeWindow) (DECLARE_VST_DEPRECATED (VstWindow)*);
	virtual void DECLARE_VST_DEPRECATED (setBlockSizeAndSampleRate) (VstInt32 _blockSize, float _sampleRate) { blockSize = _blockSize; sampleRate = _sampleRate; }
	virtual bool DECLARE_VST_DEPRECATED (getErrorText) (char* text) { return false; }
	virtual void* DECLARE_VST_DEPRECATED (getIcon) () { return 0; }
	virtual bool DECLARE_VST_DEPRECATED (setViewPosition) (VstInt32 x, VstInt32 y) { return false; }
	virtual VstInt32 DECLARE_VST_DEPRECATED (fxIdle) () { return 0; }
	virtual bool DECLARE_VST_DEPRECATED (keysRequired) () { return false; }

#if VST_2_2_EXTENSIONS
	virtual bool DECLARE_VST_DEPRECATED (getChunkFile) (void* nativePath);		///< Returns in platform format the path of the current chunk (could be called in #setChunk ()) (FSSpec on MAC else char*)
#endif // VST_2_2_EXTENSIONS
/// @endcond
//-------------------------------------------------------------------------------------------------------
};

#endif //__audioeffectx__

#include "vstAudioEffect.h"

#include "version.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

#include "../virusLib/midiTypes.h"

#ifndef _MSC_VER
#define _stricmp strcasecmp
#endif

//-------------------------------------------------------------------------------------------------------
AudioEffect* createEffectInstance (audioMasterCallback audioMaster)
{
	return new VSTAudioEffect (audioMaster);
}

//-------------------------------------------------------------------------------------------------------
VSTAudioEffect::VSTAudioEffect (audioMasterCallback am)
: AudioEffectX		(am, 0, 0)	// 0 programs, 0 parameters
{
	LOG( "Initializing plugin" );

	setNumInputs  (2);					// stereo input
	setNumOutputs (2);					// stereo output

	setUniqueID   ('tusV');				// identify - The Unknown Suspects Virus
	canProcessReplacing ();				// supports replacing output

	isSynth();

	programsAreChunks();

	setSampleRate( getSampleRate() );

	LOG( "Initialization completed" );
}

// _____________________________________________________________________________
// ~MyVSTAudioEffect
// 
VSTAudioEffect::~VSTAudioEffect()
{
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::setProgramName (char* name)
{
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::getProgramName (char* name)
{
	getProgramNameIndexed(0, getProgram(), name);
}

bool VSTAudioEffect::getProgramNameIndexed(VstInt32 category, VstInt32 index, char* text)
{
	return false;
}

void VSTAudioEffect::setProgram(VstInt32 program)
{
	AudioEffectX::setProgram(program);
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::setParameter (VstInt32 index, float value)
{
}

//-----------------------------------------------------------------------------------------
float VSTAudioEffect::getParameter (VstInt32 index)
{
	return 0.0f;
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::getParameterName (VstInt32 index, char* label)
{
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::getParameterDisplay (VstInt32 index, char* text)
{
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::getParameterLabel (VstInt32 index, char* label)
{
}

//------------------------------------------------------------------------
bool VSTAudioEffect::getEffectName (char* name)
{
	vst_strncpy (name, "Virus Emu", kVstMaxEffectNameLen);
	return true;
}

//------------------------------------------------------------------------
bool VSTAudioEffect::getProductString (char* text)
{
	vst_strncpy (text, "Virus Emu", kVstMaxProductStrLen);
	return true;
}

//------------------------------------------------------------------------
bool VSTAudioEffect::getVendorString (char* text)
{
	vst_strncpy (text, "The Usual Suspects", kVstMaxVendorStrLen);
	return true;
}

//-----------------------------------------------------------------------------------------
VstInt32 VSTAudioEffect::getVendorVersion ()
{ 
	return VERSIONVST; 
}

// _____________________________________________________________________________
// getPlugCategory
//
VstPlugCategory VSTAudioEffect::getPlugCategory()
{
	return kPlugCategSynth;
}

//-----------------------------------------------------------------------------------------
void VSTAudioEffect::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	m_plugin.process(inputs, outputs, sampleFrames);
}

// _____________________________________________________________________________
// getChunk
//
VstInt32 VSTAudioEffect::getChunk( void** _data, bool _isPreset /*= false*/ )
{
	m_chunkData.resize(1, 0);
	*_data = &m_chunkData[0];
	return m_chunkData.size();
}

// _____________________________________________________________________________
// setChunk
//
VstInt32 VSTAudioEffect::setChunk( void* _data, VstInt32 _byteSize, bool _isPreset /*= false*/ )
{
	return AudioEffectX::setChunk(_data,_byteSize,_isPreset);
}

// _____________________________________________________________________________
// setSampleRate
//
void VSTAudioEffect::setSampleRate( float _sampleRate )
{
	m_plugin.setSamplerate(_sampleRate);
}

// _____________________________________________________________________________
// canDo
//
VstInt32 VSTAudioEffect::canDo( char* text )
{
	if( !_stricmp( text, "receiveVstMidiEvent" ) )		return 1;
	if( !_stricmp( text, "receiveVstMidiEvents" ) )		return 1;
	if( !_stricmp( text, "receiveVstEvents" ) )			return 1;
	if( !_stricmp( text, "receiveVstTimeInfo" ) )		return 1;
	if( !_stricmp (text, "2in2out") )					return 1;
	if( !_stricmp (text, "0in2out") )					return 1;
	if( !_stricmp (text, "0in8out"))					return 1;
	if( !_stricmp (text, "2in8out"))					return 1;
	if( !_stricmp (text, "sendVstMidiEvent") )			return 1;
	if( !_stricmp (text, "sendVstMidiEvents") )			return 1;
	if( !_stricmp (text, "sendVstEvents") )				return 1;	

	LOG("canDo: " << text);

	return AudioEffectX::canDo(text);
}
// _____________________________________________________________________________
// processEvents
//
VstInt32 VSTAudioEffect::processEvents( VstEvents* events )
{
	/*
	Handling Events
	..., the host will call the processEvents() just before calling either process() or processReplacing().
	The method processEvents() has a block of VstEvents as its parameter. Each of these events in the block
	has a delta-time, in samples, that refers to positions into the audio block about to be processed with
	process() or processReplacing()...
	*/
	for( VstInt32 i=0; i<events->numEvents; ++i )
	{
		const VstEvent* eve = events->events[i];

		if( eve->type == kVstMidiType )
		{
			const auto* ev = (const VstMidiEvent*)eve;

			virusLib::SMidiEvent midiEvent;
			midiEvent.a = ev->midiData[0];
			midiEvent.b = ev->midiData[1];
			midiEvent.c = ev->midiData[2];
			midiEvent.offset = ev->deltaFrames;

			m_plugin.addMidiEvent(midiEvent);
//			LOG("Midi " << (int)midiEvent.a << " " << (int)midiEvent.b << " " << (int)midiEvent.c);
		}
		else if( eve->type == kVstSysExType )
		{
			const VstMidiSysexEvent* ev = (const VstMidiSysexEvent*)eve;

			LOG("Got Sysex! Length " << ev->dumpBytes);

			virusLib::SMidiEvent midiEvent;
			midiEvent.sysex.resize(ev->dumpBytes);
			midiEvent.offset = ev->deltaFrames;

			memcpy(&midiEvent.sysex[0], ev->sysexDump, ev->dumpBytes);

			m_plugin.addMidiEvent(midiEvent);
		}
		else
		{
			LOG("Unknown event type " << eve->type);
		}
	}

	return 1; // want more
}

void VSTAudioEffect::sendMidi(const virusLib::SMidiEvent& _midi)
{
	std::vector<virusLib::SMidiEvent> events;
	events.push_back(_midi);
	sendMidiEventsToHost(events);
}

void VSTAudioEffect::sendMidiEventsToHost(const std::vector<virusLib::SMidiEvent>& _midiEvents)
{
	// send midi back to VST host
	if (_midiEvents.empty())
		return;

	{
		std::vector<VstMidiEvent> events;
		events.reserve(_midiEvents.size());

		for (size_t i = 0; i < _midiEvents.size(); ++i)
		{
			const auto& ev = _midiEvents[i];

			if (!ev.sysex.empty())
				continue;

			VstMidiEvent dst{};

			dst.type = kVstMidiType;
			dst.byteSize = sizeof(dst);

			dst.deltaFrames = ev.offset;

			dst.midiData[0] = ev.a;
			dst.midiData[1] = ev.b;
			dst.midiData[2] = ev.c;
			dst.midiData[3] = 0;

			events.push_back(dst);
		}

		if (!events.empty())
		{
			// we worst API i've seen in my entire C/C++ career:
			auto vstEvents = static_cast<VstEvents*>(malloc(sizeof(VstEvents) + sizeof(VstMidiEvent*) * events.size()));
			memset(vstEvents, 0, sizeof(VstEvents));

			vstEvents->numEvents = static_cast<VstInt32>(events.size());

			for (size_t i = 0; i < events.size(); ++i)
				vstEvents->events[i] = reinterpret_cast<VstEvent*>(&events[i]);

			sendVstEventsToHost(vstEvents);

			free(vstEvents);
		}
	}
	{
		std::vector<VstMidiSysexEvent> events;
		events.reserve(_midiEvents.size());

		for (size_t i = 0; i < _midiEvents.size(); ++i)
		{
			const auto& ev = _midiEvents[i];

			if (ev.sysex.empty())
				continue;

			VstMidiSysexEvent dst{};

			dst.type = kVstSysExType;
			dst.byteSize = sizeof(dst);

			dst.deltaFrames = ev.offset;

			dst.sysexDump = (char*)&ev.sysex[0];
			dst.dumpBytes = ev.sysex.size();

			events.push_back(dst);
		}

		if (!events.empty())
		{
			// and another time, now for sysex
			auto vstEvents = static_cast<VstEvents*>(malloc(sizeof(VstEvents) + sizeof(VstMidiSysexEvent*) * events.size()));
			memset(vstEvents, 0, sizeof(VstEvents));

			vstEvents->numEvents = static_cast<VstInt32>(events.size());

			for (size_t i = 0; i < events.size(); ++i)
				vstEvents->events[i] = reinterpret_cast<VstEvent*>(&events[i]);

			sendVstEventsToHost(vstEvents);

			free(vstEvents);
		}
	}
}

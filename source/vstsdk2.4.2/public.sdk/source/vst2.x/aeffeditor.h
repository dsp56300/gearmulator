//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4       $Date: 2006/01/12 09:05:31 $
//
// Category     : VST 2.x Classes
// Filename     : aeffeditor.h
// Created by   : Steinberg Media Technologies
// Description  : Editor Class for VST Plug-Ins
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __aeffeditor__
#define __aeffeditor__

#include "audioeffectx.h"

//-------------------------------------------------------------------------------------------------------
/** VST Effect Editor class. */
//-------------------------------------------------------------------------------------------------------
class AEffEditor
{
public:
//-------------------------------------------------------------------------------------------------------
	AEffEditor (AudioEffect* effect = 0)	///< Editor class constructor. Requires pointer to associated effect instance.
	: effect (effect)
	, systemWindow (0)
	{}

	virtual ~AEffEditor () ///< Editor class destructor.
	{}

	virtual AudioEffect* getEffect ()	{ return effect; }					///< Returns associated effect instance
	virtual bool getRect (ERect** rect)	{ *rect = 0; return false; }		///< Query editor size as #ERect
	virtual bool open (void* ptr)		{ systemWindow = ptr; return 0; }	///< Open editor, pointer to parent windows is platform-dependent (HWND on Windows, WindowRef on Mac).
	virtual void close ()				{ systemWindow = 0; }				///< Close editor (detach from parent window)
	virtual bool isOpen ()				{ return systemWindow != 0; }		///< Returns true if editor is currently open
	virtual void idle ()				{}									///< Idle call supplied by Host application

#if TARGET_API_MAC_CARBON
	virtual void DECLARE_VST_DEPRECATED (draw) (ERect* rect) {}
	virtual VstInt32 DECLARE_VST_DEPRECATED (mouse) (VstInt32 x, VstInt32 y) { return 0; }
	virtual VstInt32 DECLARE_VST_DEPRECATED (key) (VstInt32 keyCode) { return 0; }
	virtual void DECLARE_VST_DEPRECATED (top) () {}
	virtual void DECLARE_VST_DEPRECATED (sleep) () {}
#endif

#if VST_2_1_EXTENSIONS
	virtual bool onKeyDown (VstKeyCode& keyCode)	{ return false; }		///< Receive key down event. Return true only if key was really used!
	virtual bool onKeyUp (VstKeyCode& keyCode)		{ return false; }		///< Receive key up event. Return true only if key was really used!
	virtual bool onWheel (float distance)			{ return false; }		///< Handle mouse wheel event, distance is positive or negative to indicate wheel direction.
	virtual bool setKnobMode (VstInt32 val)			{ return false; }		///< Set knob mode (if supported by Host). See CKnobMode in VSTGUI.
#endif

//-------------------------------------------------------------------------------------------------------
protected:
	AudioEffect* effect;	///< associated effect instance
	void* systemWindow;		///< platform-dependent parent window (HWND or WindowRef)
};

#endif // __aeffeditor__

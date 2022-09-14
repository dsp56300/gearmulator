//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:28 $
//
// Category     : VST 2.x SDK Samples
// Filename     : sdeditor.cpp
// Created by   : Steinberg Media Technologies
// Description  : Simple Surround Delay plugin with Editor using VSTGUI
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __sdeditor__
#include "sdeditor.h"
#endif

#ifndef __adelay__
#include "../adelay.h"
#endif

#include <stdio.h>

//-----------------------------------------------------------------------------
// resource id's
enum {
	// bitmaps
	kBackgroundId = 128,
	kFaderBodyId,
	kFaderHandleId,
	
	// positions
	kFaderX = 18,
	kFaderY = 10,

	kFaderInc = 18,

	kDisplayX = 10,
	kDisplayY = 184,
	kDisplayXWidth = 30,
	kDisplayHeight = 14
};

//-----------------------------------------------------------------------------
// prototype string convert float -> percent
void percentStringConvert (float value, char* string);
void percentStringConvert (float value, char* string)
{
	 sprintf (string, "%d%%", (int)(100 * value + 0.5f));
}


//-----------------------------------------------------------------------------
// SDEditor class implementation
//-----------------------------------------------------------------------------
SDEditor::SDEditor (AudioEffect *effect)
 : AEffGUIEditor (effect) 
{
	delayFader    = 0;
	feedbackFader = 0;
	volumeFader   = 0;
	delayDisplay  = 0;
	feedbackDisplay = 0;
	volumeDisplay = 0;

	// load the background bitmap
	// we don't need to load all bitmaps, this could be done when open is called
	hBackground = new CBitmap (kBackgroundId);

	// init the size of the plugin
	rect.left   = 0;
	rect.top    = 0;
	rect.right  = (short)hBackground->getWidth ();
	rect.bottom = (short)hBackground->getHeight ();
}

//-----------------------------------------------------------------------------
SDEditor::~SDEditor ()
{
	// free the background bitmap
	if (hBackground)
		hBackground->forget ();
	hBackground = 0;
}

//-----------------------------------------------------------------------------
bool SDEditor::open (void *ptr)
{
	// !!! always call this !!!
	AEffGUIEditor::open (ptr);
	
	//--load some bitmaps
	CBitmap* hFaderBody   = new CBitmap (kFaderBodyId);
	CBitmap* hFaderHandle = new CBitmap (kFaderHandleId);

	//--init background frame-----------------------------------------------
	// We use a local CFrame object so that calls to setParameter won't call into objects which may not exist yet. 
	// If all GUI objects are created we assign our class member to this one. See bottom of this method.
	CRect size (0, 0, hBackground->getWidth (), hBackground->getHeight ());
	CFrame* lFrame = new CFrame (size, ptr, this);
	lFrame->setBackground (hBackground);

	//--init the faders------------------------------------------------
	int minPos = kFaderY;
	int maxPos = kFaderY + hFaderBody->getHeight () - hFaderHandle->getHeight () - 1;
	CPoint point (0, 0);
	CPoint offset (1, 0);

	// Delay
	size (kFaderX, kFaderY,
          kFaderX + hFaderBody->getWidth (), kFaderY + hFaderBody->getHeight ());
	delayFader = new CVerticalSlider (size, this, kDelay, minPos, maxPos, hFaderHandle, hFaderBody, point);
	delayFader->setOffsetHandle (offset);
	delayFader->setValue (effect->getParameter (kDelay));
	lFrame->addView (delayFader);

	// FeedBack
	size.offset (kFaderInc + hFaderBody->getWidth (), 0);
	feedbackFader = new CVerticalSlider (size, this, kFeedBack, minPos, maxPos, hFaderHandle, hFaderBody, point);
	feedbackFader->setOffsetHandle (offset);
	feedbackFader->setValue (effect->getParameter (kFeedBack));
	lFrame->addView (feedbackFader);

	// Volume
	size.offset (kFaderInc + hFaderBody->getWidth (), 0);
	volumeFader = new CVerticalSlider (size, this, kOut, minPos, maxPos, hFaderHandle, hFaderBody, point);
	volumeFader->setOffsetHandle (offset);
	volumeFader->setValue (effect->getParameter (kOut));
	volumeFader->setDefaultValue (0.75f);
	lFrame->addView (volumeFader);

	//--init the display------------------------------------------------
	// Delay
	size (kDisplayX, kDisplayY,
          kDisplayX + kDisplayXWidth, kDisplayY + kDisplayHeight);
	delayDisplay = new CParamDisplay (size, 0, kCenterText);
	delayDisplay->setFont (kNormalFontSmall);
	delayDisplay->setFontColor (kWhiteCColor);
	delayDisplay->setBackColor (kBlackCColor);
	delayDisplay->setFrameColor (kBlueCColor);
	delayDisplay->setValue (effect->getParameter (kDelay));
	lFrame->addView (delayDisplay);

	// FeedBack
	size.offset (kFaderInc + hFaderBody->getWidth (), 0);
	feedbackDisplay = new CParamDisplay (size, 0, kCenterText);
	feedbackDisplay->setFont (kNormalFontSmall);
	feedbackDisplay->setFontColor (kWhiteCColor);
	feedbackDisplay->setBackColor (kBlackCColor);
	feedbackDisplay->setFrameColor (kBlueCColor);
	feedbackDisplay->setValue (effect->getParameter (kFeedBack));
	feedbackDisplay->setStringConvert (percentStringConvert);
	lFrame->addView (feedbackDisplay);

	// Volume
	size.offset (kFaderInc + hFaderBody->getWidth (), 0);
	volumeDisplay = new CParamDisplay (size, 0, kCenterText);
	volumeDisplay->setFont (kNormalFontSmall);
	volumeDisplay->setFontColor (kWhiteCColor);
	volumeDisplay->setBackColor (kBlackCColor);
	volumeDisplay->setFrameColor (kBlueCColor);
	volumeDisplay->setValue (effect->getParameter (kOut));
	volumeDisplay->setStringConvert (percentStringConvert);
	lFrame->addView (volumeDisplay);


	// Note : in the constructor of a CBitmap, the number of references is set to 1.
	// Then, each time the bitmap is used (for hinstance in a vertical slider), this
	// number is incremented.
	// As a consequence, everything happens as if the constructor by itself was adding
	// a reference. That's why we need til here a call to forget ().
	// You mustn't call delete here directly, because the bitmap is used by some CControls...
	// These "rules" apply to the other VSTGUI objects too.
	hFaderBody->forget ();
	hFaderHandle->forget ();

	frame = lFrame;
	return true;
}

//-----------------------------------------------------------------------------
void SDEditor::close ()
{
	delete frame;
	frame = 0;
}

//-----------------------------------------------------------------------------
void SDEditor::setParameter (VstInt32 index, float value)
{
	if (frame == 0)
		return;

	// called from ADelayEdit
	switch (index)
	{
		case kDelay:
			if (delayFader)
				delayFader->setValue (effect->getParameter (index));
			if (delayDisplay)
				delayDisplay->setValue (effect->getParameter (index));
			break;

		case kFeedBack:
			if (feedbackFader)
				feedbackFader->setValue (effect->getParameter (index));
			if (feedbackDisplay)
				feedbackDisplay->setValue (effect->getParameter (index));
			break;

		case kOut:
			if (volumeFader)
				volumeFader->setValue (effect->getParameter (index));
			if (volumeDisplay)
				volumeDisplay->setValue (effect->getParameter (index));
			break;
	}
}

//-----------------------------------------------------------------------------
void SDEditor::valueChanged (CDrawContext* context, CControl* control)
{
	long tag = control->getTag ();
	switch (tag)
	{
		case kDelay:
		case kFeedBack:
		case kOut:
			effect->setParameterAutomated (tag, control->getValue ());
			control->setDirty ();
		break;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

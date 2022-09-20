//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2006/11/13 09:08:28 $
//
// Category     : VST 2.x SDK Samples
// Filename     : minieditor.cpp
// Created by   : Steinberg
// Description  : VST Mini Host Editor
//
// © 2006, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#include "pluginterfaces/vst2.x/aeffectx.h"

#if _WIN32
#include <windows.h>
#elif TARGET_API_MAC_CARBON
#include <Carbon/Carbon.h>
static pascal OSStatus windowHandler (EventHandlerCallRef inHandlerCallRef, EventRef inEvent, void* inUserData);
static pascal void idleTimerProc (EventLoopTimerRef inTimer, void* inUserData);
#endif

#include <stdio.h>

#if _WIN32
//-------------------------------------------------------------------------------------------------------
struct MyDLGTEMPLATE: DLGTEMPLATE
{
	WORD ext[3];
	MyDLGTEMPLATE ()
	{
		memset (this, 0, sizeof (*this));
	};
};

static INT_PTR CALLBACK EditorProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static AEffect* theEffect = 0;
#endif

//-------------------------------------------------------------------------------------------------------
bool checkEffectEditor (AEffect* effect)
{
	if ((effect->flags & effFlagsHasEditor) == 0)
	{
		printf ("This plug does not have an editor!\n");
		return false;
	}

#if _WIN32
	theEffect = effect;

	MyDLGTEMPLATE t;	
	t.style = WS_POPUPWINDOW|WS_DLGFRAME|DS_MODALFRAME|DS_CENTER;
	t.cx = 100;
	t.cy = 100;
	DialogBoxIndirectParam (GetModuleHandle (0), &t, 0, (DLGPROC)EditorProc, (LPARAM)effect);

	theEffect = 0;
#elif TARGET_API_MAC_CARBON
	WindowRef window;
	Rect mRect = {0, 0, 300, 300};
	OSStatus err = CreateNewWindow (kDocumentWindowClass, kWindowCloseBoxAttribute | kWindowCompositingAttribute | kWindowAsyncDragAttribute | kWindowStandardHandlerAttribute, &mRect, &window);
	if (err != noErr)
	{
		printf ("HOST> Could not create mac window !\n");
		return false;
	}
	static EventTypeSpec eventTypes[] = {
		{ kEventClassWindow, kEventWindowClose }
	};
	InstallWindowEventHandler (window, windowHandler, GetEventTypeCount (eventTypes), eventTypes, window, NULL);

	printf ("HOST> Open editor...\n");
	effect->dispatcher (effect, effEditOpen, 0, 0, window, 0);
	ERect* eRect = 0;
	printf ("HOST> Get editor rect..\n");
	effect->dispatcher (effect, effEditGetRect, 0, 0, &eRect, 0);
	if (eRect)
	{
		int width = eRect->right - eRect->left;
		int height = eRect->bottom - eRect->top;
		Rect bounds;
		GetWindowBounds (window, kWindowContentRgn, &bounds);
		bounds.right = bounds.left + width;
		bounds.bottom = bounds.top + height;
		SetWindowBounds (window, kWindowContentRgn, &bounds); 
	}
	RepositionWindow (window, NULL, kWindowCenterOnMainScreen);
	ShowWindow (window);

	EventLoopTimerRef idleEventLoopTimer;
	InstallEventLoopTimer (GetCurrentEventLoop (), kEventDurationSecond / 25., kEventDurationSecond / 25., idleTimerProc, effect, &idleEventLoopTimer);

	RunAppModalLoopForWindow (window);
	RemoveEventLoopTimer (idleEventLoopTimer);
	
	printf ("HOST> Close editor..\n");
	effect->dispatcher (effect, effEditClose, 0, 0, 0, 0);
	ReleaseWindow (window);
#endif
	return true;
}

#if _WIN32
//-------------------------------------------------------------------------------------------------------
INT_PTR CALLBACK EditorProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	AEffect* effect = theEffect;

	switch(msg)
	{
		//-----------------------
		case WM_INITDIALOG :
		{
			SetWindowText (hwnd, "VST Editor");
			SetTimer (hwnd, 1, 20, 0);

			if (effect)
			{
				printf ("HOST> Open editor...\n");
				effect->dispatcher (effect, effEditOpen, 0, 0, hwnd, 0);

				printf ("HOST> Get editor rect..\n");
				ERect* eRect = 0;
				effect->dispatcher (effect, effEditGetRect, 0, 0, &eRect, 0);
				if (eRect)
				{
					int width = eRect->right - eRect->left;
					int height = eRect->bottom - eRect->top;
					if (width < 100)
						width = 100;
					if (height < 100)
						height = 100;

					RECT wRect;
					SetRect (&wRect, 0, 0, width, height);
					AdjustWindowRectEx (&wRect, GetWindowLong (hwnd, GWL_STYLE), FALSE, GetWindowLong (hwnd, GWL_EXSTYLE));
					width = wRect.right - wRect.left;
					height = wRect.bottom - wRect.top;

					SetWindowPos (hwnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE);
				}
			}
		}	break;

		//-----------------------
		case WM_TIMER :
			if (effect)
				effect->dispatcher (effect, effEditIdle, 0, 0, 0, 0);
			break;

		//-----------------------
		case WM_CLOSE :
		{
			KillTimer (hwnd, 1);

			printf ("HOST> Close editor..\n");
			if (effect)
				effect->dispatcher (effect, effEditClose, 0, 0, 0, 0);

			EndDialog (hwnd, IDOK);
		}	break;
	}

	return 0;
}

#elif TARGET_API_MAC_CARBON
//-------------------------------------------------------------------------------------------------------
pascal void idleTimerProc (EventLoopTimerRef inTimer, void *inUserData)
{
	AEffect* effect = (AEffect*)inUserData;
	effect->dispatcher (effect, effEditIdle, 0, 0, 0, 0);
}

//-------------------------------------------------------------------------------------------------------
pascal OSStatus windowHandler (EventHandlerCallRef inHandlerCallRef, EventRef inEvent, void *inUserData)
{
	OSStatus result = eventNotHandledErr;
	WindowRef window = (WindowRef) inUserData;
	UInt32 eventClass = GetEventClass (inEvent);
	UInt32 eventKind = GetEventKind (inEvent);

	switch (eventClass)
	{
		case kEventClassWindow:
		{
			switch (eventKind)
			{
				case kEventWindowClose:
				{
					QuitAppModalLoopForWindow (window);
					break;
				}
			}
			break;
		}
	}

	return result;
}

#endif

# Microsoft Developer Studio Project File - Name="surrounddelay" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=surrounddelay - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "surrounddelay.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "surrounddelay.mak" CFG="surrounddelay - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "surrounddelay - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "surrounddelay - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "surrounddelay - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release/surrounddelay"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SURROUNDDELAY_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../../../.." /I "..\..\..\vstgui" /I "..\..\..\..\public.sdk\source\vst2.x" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SURROUNDDELAY_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x407 /d "NDEBUG"
# ADD RSC /l 0x407 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386

!ELSEIF  "$(CFG)" == "surrounddelay - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "surrounddelay___Win32_Debug"
# PROP BASE Intermediate_Dir "surrounddelay___Win32_Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug/surrounddelay"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SURROUNDDELAY_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../../.." /I "..\..\..\vstgui" /I "..\..\..\..\public.sdk\source\vst2.x" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SURROUNDDELAY_EXPORTS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x407 /d "_DEBUG"
# ADD RSC /l 0x407 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "surrounddelay - Win32 Release"
# Name "surrounddelay - Win32 Debug"
# Begin Group "Interfaces"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\..\pluginterfaces\vst2.x\aeffect.h
# End Source File
# Begin Source File

SOURCE=..\..\..\..\pluginterfaces\vst2.x\aeffectx.h
# End Source File
# Begin Source File

SOURCE=..\..\..\..\pluginterfaces\vst2.x\vstfxstore.h
# End Source File
# End Group
# Begin Group "Source Files"

# PROP Default_Filter ""
# Begin Group "vst2.x"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\source\vst2.x\aeffeditor.h
# End Source File
# Begin Source File

SOURCE=..\..\..\source\vst2.x\audioeffect.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\source\vst2.x\audioeffect.h
# End Source File
# Begin Source File

SOURCE=..\..\..\source\vst2.x\audioeffectx.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\source\vst2.x\audioeffectx.h
# End Source File
# Begin Source File

SOURCE=..\..\..\source\vst2.x\vstplugmain.cpp
# End Source File
# End Group
# Begin Group "vstgui"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\..\vstgui.sf\vstgui\aeffguieditor.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\..\vstgui.sf\vstgui\aeffguieditor.h
# End Source File
# Begin Source File

SOURCE=..\..\..\..\vstgui.sf\vstgui\vstcontrols.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\..\vstgui.sf\vstgui\vstcontrols.h
# End Source File
# Begin Source File

SOURCE=..\..\..\..\vstgui.sf\vstgui\vstgui.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\..\vstgui.sf\vstgui\vstgui.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\adelay\adelay.cpp
# End Source File
# Begin Source File

SOURCE=..\adelay\adelay.h
# End Source File
# Begin Source File

SOURCE=..\adelay\editor\sdeditor.cpp
# End Source File
# Begin Source File

SOURCE=..\adelay\editor\sdeditor.h
# End Source File
# Begin Source File

SOURCE=..\adelay\surrounddelay.cpp
# End Source File
# Begin Source File

SOURCE=..\adelay\surrounddelay.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\adelay\editor\resources\surrounddelay.rc
# End Source File
# Begin Source File

SOURCE=..\win\vstplug.def
# End Source File
# End Target
# End Project

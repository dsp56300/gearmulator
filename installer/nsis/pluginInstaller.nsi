!include LogicLib.nsh

!define		NAME		Osirus
!define		VENDOR		"The Usual Suspects"

!define		DIR_PLUGINS	"..\..\bin\plugins\Release"
!define		DIR_SKINS	"..\..\source\osirusJucePlugin\skins"

Name		${NAME}

#!include ${NAME}.nsh

OutFile		${NAME}Installer.exe

InstallColors	/windows
XPStyle		on
InstallDir 	"$PROGRAMFILES64\${VENDOR}\${NAME}"

RequestExecutionLevel admin

# ____________________________
#

Var DirVst2
Var DirVst3
Var DirClap
Var DirLv2

Function .onInit
	SetShellVarContext current
FunctionEnd

# ____________________________
# SECTIONS
#

SectionGroup /e "Plugin Formats"
	Section "VST2" Sec_Vst2
		SetOutPath $DirVst2
		File /r /x *.so /x *.pdb "${DIR_PLUGINS}\VST\${NAME}.dll"
	SectionEnd

	Section "VST3" Sec_Vst3
		SetOutPath $DirVst3
		File /r /x *.so /x *.pdb "${DIR_PLUGINS}\VST3\${NAME}.vst3"
	SectionEnd

	Section "CLAP" Sec_Clap
		SetOutPath $DirClap
		File /r /x *.so /x *.pdb "${DIR_PLUGINS}\CLAP\${NAME}.clap"
	SectionEnd

	Section "LV2" Sec_Lv2
		SetOutPath $DirLv2
		File /r /x *.so /x *.pdb "${DIR_PLUGINS}\LV2\${NAME}.lv2"
	SectionEnd
SectionGroupEnd

Section "Default Skins"
	SetOutPath "$DOCUMENTS\${VENDOR}\${NAME}\skins"
	File /r "${DIR_SKINS}"
SectionEnd

Section
	SetOutPath "$PROGRAMFILES64\${VENDOR}\${NAME}"
	WriteUninstaller "$INSTDIR\${NAME}Uninstaller.exe"
	CreateShortcut "$SMPROGRAMS\Uninstall ${NAME}.lnk" "$INSTDIR\${NAME}Uninstaller.exe"
SectionEnd

# ____________________________
# PAGES
#

Page components

Page directory

!macro PagePlugin Result PluginName SectionName DefaultPath
	PageEx directory
		Caption " - Select ${PluginName} directory"
		DirVar $${Result}
		PageCallbacks DirPluginPre${PluginName} "" ""
	PageExEnd

	Function DirPluginPre${PluginName}
		${If} ${SectionIsSelected} ${SectionName}
			StrCpy $${Result} "${DefaultPath}"
		${Else}
			Abort
		${EndIf}
	FunctionEnd
!macroend

!insertmacro PagePlugin DirVst2 VST2 ${Sec_Vst2} "C:\Program Files\Common Files\VST2"
!insertmacro PagePlugin DirVst3 VST3 ${Sec_Vst3} "C:\Program Files\Common Files\VST3"
!insertmacro PagePlugin DirClap CLAP ${Sec_Clap} "C:\Program Files\Common Files\CLAP"
!insertmacro PagePlugin DirLv2 LV2 ${Sec_Lv2} "C:\Program Files\Common Files\LV2"

Page instfiles

# ____________________________
# UNINSTALLER
# 

Section "uninstall"
	Delete "$SMPROGRAMS\Uninstall ${NAME}.lnk"
	Delete "$INSTDIR\${NAME}Uninstaller.exe"
	RMDir $INSTDIR
SectionEnd

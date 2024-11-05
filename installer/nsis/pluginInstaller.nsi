!include LogicLib.nsh

!define				NAME			Osirus
!define				VENDOR			"The Usual Suspects"
	
!define				DIR_PLUGINS		"..\..\bin\plugins\Release"
!define				DIR_SKINS		"..\..\source\osirusJucePlugin\skins\"
!define				DIR_DATA		"$DOCUMENTS\${VENDOR}\${NAME}"

Name				${NAME}
	
#!include			${NAME}.nsh
	
!define				REGKEYBASE		"Software\${VENDOR}\${NAME}"
	
OutFile				${NAME}Installer.exe
	
InstallColors		/windows
XPStyle				on

InstallDir 			"$PROGRAMFILES64\${VENDOR}\${NAME}"
InstallDirRegKey	HKLM "${REGKEYBASE}" "InstallDir"

RequestExecutionLevel	admin

# ____________________________
#

Var DirVst2
Var DirVst3
Var DirClap
Var DirLv2

Function .onInit
	SetShellVarContext current
	SetRegView 64
FunctionEnd

Function un.onInit
	SetShellVarContext current
	SetRegView 64
FunctionEnd

# ____________________________
# SECTIONS
#

!ifdef DIR_SKINS
Section "Default Skins (required)"
	SectionIn RO
	SetOutPath "${DIR_DATA}\skins"
	File /r "${DIR_SKINS}"
SectionEnd
!endif

/*
SectionGroup /e "Variants"
	Section "Instrument" Sec_Instruments
	SectionEnd

	Section "FX" Sec_FX
	SectionEnd
SectionGroupEnd
*/
!macro SectionPlugin Format SectionVarName DirVarName FileExtension
	Section "${Format}" ${SectionVarName}
		SetOutPath $${DirVarName}
#		${If} ${SectionIsSelected} ${Sec_Instruments}
			File /r /x *.so /x *.pdb "${DIR_PLUGINS}\${Format}\${NAME}.${FileExtension}"
#		${EndIf}
#		${If} ${SectionIsSelected} ${Sec_FX}
			File /r /x *.so /x *.pdb "${DIR_PLUGINS}\${Format}\${NAME}FX.${FileExtension}"
#		${EndIf}
		WriteRegStr HKLM "${REGKEYBASE}" "InstallDir${Format}" $${DirVarName}
	SectionEnd
!macroend

SectionGroup /e "Plugin Formats"
	!insertmacro SectionPlugin "VST" Sec_Vst2 DirVst2 dll
	!insertmacro SectionPlugin "VST3" Sec_Vst3 DirVst3 vst3
	!insertmacro SectionPlugin "CLAP" Sec_Clap DirClap clap
	!insertmacro SectionPlugin "LV2" Sec_Lv2 DirLv2 lv2
SectionGroupEnd

Section
	SetOutPath "$INSTDIR"
	WriteRegStr HKLM "${REGKEYBASE}" "InstallDir" "$INSTDIR"
	WriteUninstaller "$INSTDIR\${NAME}Uninstaller.exe"
	CreateShortcut "$SMPROGRAMS\Uninstall ${NAME}.lnk" "$INSTDIR\${NAME}Uninstaller.exe"
SectionEnd

# ____________________________
# PAGES
#

Page components

Page directory

!macro PagePlugin Result Format SectionName DefaultPath
	PageEx directory
		Caption " - Select ${Format} directory"
		DirVar $${Result}
		PageCallbacks DirPluginPre${Format} "" ""
	PageExEnd

	Function DirPluginPre${Format}
		${If} ${SectionIsSelected} ${SectionName}
			ReadRegStr $${Result} HKLM "${REGKEYBASE}" "InstallDir${Format}"
			${If} $${Result} == ""
				StrCpy $${Result} "${DefaultPath}"
			${EndIf}
		${Else}
			Abort
		${EndIf}
	FunctionEnd
!macroend

!insertmacro PagePlugin DirVst2 VST ${Sec_Vst2} "$COMMONFILES64\VST2"
!insertmacro PagePlugin DirVst3 VST3 ${Sec_Vst3} "$COMMONFILES64\VST3"
!insertmacro PagePlugin DirClap CLAP ${Sec_Clap} "$COMMONFILES64\CLAP"
!insertmacro PagePlugin DirLv2 LV2 ${Sec_Lv2} "$COMMONFILES64\LV2"

Page instfiles

# ____________________________
# UNINSTALLER
# 

Section "un.Remove Skins"
	RMDir /r "${DIR_DATA}\skins"
SectionEnd

Section "un.Remove Firmware ROMs"
	RMDir /r "${DIR_DATA}\roms"
SectionEnd

!macro RemovePlugin Format
	ReadRegStr $0 HKLM "${REGKEYBASE}" "InstallDir${Format}"
	${If} "$0" != ""
		RMDir /r "$0\${NAME}.${Format}"
		RMDir /r "$0\${NAME}FX.${Format}"
		Delete "$0\${NAME}.dll"
		Delete "$0\${NAME}FX.dll"
	${EndIf}
!macroend

Section "un.Base Files (always removed)"
	!insertmacro RemovePlugin VST
	!insertmacro RemovePlugin VST3
	!insertmacro RemovePlugin CLAP
	!insertmacro RemovePlugin LV2
	Delete "$SMPROGRAMS\Uninstall ${NAME}.lnk"
	Delete "$INSTDIR\${NAME}Uninstaller.exe"
	RMDir "$INSTDIR"
	RMDir "${DIR_DATA}"
	DeleteRegKey HKLM "${REGKEYBASE}"
SectionEnd

UninstPage components
UninstPage instfiles

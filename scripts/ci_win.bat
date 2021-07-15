pushd ..\
call build_win64.bat
popd
7z a ..\deploy\dsp56300emuVST2Win.zip ..\temp\cmake_win64\source\jucePlugin\jucePlugin_artefacts\Release\VST\DSP56300Emu.dll
7z a ..\deploy\dsp56300emuVST3Win.zip ..\temp\cmake_win64\source\jucePlugin\jucePlugin_artefacts\Release\VST3\DSP56300Emu.vst3\Contents\x86_64-win\DSP56300Emu.vst3
7z a ..\deploy\dsp56300emuTestConsoleWin.zip ..\temp\cmake_win64\Release\virusTestConsole.exe ..\deploy\win\*.bat
IF %ERRORLEVEL% NEQ 0 exit -3

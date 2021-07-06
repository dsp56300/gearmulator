pushd ..\
call build_win64.bat
popd
..\tools\7-Zip\7z.exe a ..\deploy\VirusVST2Win.zip ..\temp\cmake_win64\Release\VirusEmuVST2x64.dll
..\tools\7-Zip\7z.exe a ..\deploy\VirusTestConsoleWin.zip ..\temp\cmake_win64\Release\virusTestConsole.exe ..\deploy\win\*.bat
IF %ERRORLEVEL% NEQ 0 exit -3

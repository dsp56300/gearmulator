set outdir=temp\cmake_win32\
cmake . -B %outdir% -G "Visual Studio 15 2017"
IF %ERRORLEVEL% NEQ 0 (
	popd 
	exit /B 2
)
pushd %outdir%
cmake --build . --config Debug
IF %ERRORLEVEL% NEQ 0 (
	popd 
	exit /B 2
)
cpack -G ZIP
popd
move /y %outdir%*.zip deploy\

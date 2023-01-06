set outdir=temp\cmake_win64\
cmake . -B %outdir% -G "Visual Studio 16 2019" -A x64 -Dgearmulator_BUILD_FX_PLUGIN=ON
IF %ERRORLEVEL% NEQ 0 (
	popd 
	exit /B 2
)
pushd %outdir%
cmake --build . --config Release
IF %ERRORLEVEL% NEQ 0 (
	popd 
	exit /B 2
)
cmake -P ../../scripts/pack.cmake
popd
move /y %outdir%*.zip deploy\
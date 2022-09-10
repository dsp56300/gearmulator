set ABI=%~1
set OUTDIR=temp\cmake_android_%ABI%

set ARGS=-DANDROID_ARM_NEON=TRUE -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK%/build/cmake/android.toolchain.cmake -DCMAKE_MAKE_PROGRAM=%ANDROID_NDK%/prebuilt/windows-x86_64/bin/make.exe -DANDROID_NATIVE_API_LEVEL=21 -B "%OUTDIR%" -G "MinGW Makefiles" -Dgearmulator_BUILD_JUCEPLUGIN=OFF 

cmake %ARGS% "-DANDROID_ABI=%ABI%"

IF %ERRORLEVEL% NEQ 0 exit /B 1
pushd %OUTDIR%
cmake --build . --config Release
popd

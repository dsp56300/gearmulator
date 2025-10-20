:: -------- setup --------

:: source folder
set IN=..\

:: build system output folder
set OUT=%~dp0..\temp\test

:: which synths should be built?
set SYNTHS=-Dgearmulator_SYNTH_OSIRUS=on -Dgearmulator_SYNTH_OSTIRUS=on -Dgearmulator_SYNTH_VAVRA=on -Dgearmulator_SYNTH_XENIA=on

:: -------- generate project configuration --------

cmake -Dgearmulator_SOURCE_DIR=%IN% -Dgearmulator_BINARY_DIR=%OUT% %SYNTHS% -Dgearmulator_BUILD_JUCEPLUGIN=on -Dgearmulator_BUILD_FX_PLUGIN=off -P generate.cmake

:: -------- build the project --------

cmake --build %OUT% --config Release --parallel 24

:: -------- run tests --------

ctest -C Release -VV --output-on-failure --test-dir %OUT%

:: -------- create packages --------

pushd %OUT%
cmake -P %~dp0/pack.cmake
popd

cmake -Dgearmulator_BINARY_DIR=%OUT% -P packagesource.cmake

:: -------- deploy packages --------

cmake -DUPLOAD_LOCAL=1 -DFOLDER=test -Dgearmulator_BINARY_DIR=%OUT% -P deployAll.cmake

cd ../
git reset --hard
git pull
git submodule update --recursive
./build_linux.sh
cd scripts
7z a ../deploy/dsp56300emuVST2Linux.zip ../temp/cmake_linux/source/jucePlugin/jucePlugin_artefacts/Release/VST/libDSP56300Emu.so
7z a ../deploy/dsp56300emuVST3Linux.zip ../temp/cmake_linux/source/jucePlugin/jucePlugin_artefacts/Release/VST3/DSP56300Emu.vst3
7z a ../deploy/dsp56300emuTestConsoleLinux.zip ../temp/cmake_linux/virusTestConsole ../deploy/linux/*.sh

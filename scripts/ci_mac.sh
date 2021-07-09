cd ../
git reset --hard
git pull
git submodule update --recursive
./build_mac.sh
cd scripts
7z a ../deploy/dsp56300emuVST2Mac.zip ../temp/cmake/source/jucePlugin/jucePlugin_artefacts/Release/VST/DSP56300Emu.vst
7z a ../deploy/dsp56300emuVST3Mac.zip ../temp/cmake/source/jucePlugin/jucePlugin_artefacts/Release/VST3/DSP56300Emu.vst3
7z a ../deploy/dsp56300emuAUMac.zip ../temp/cmake/source/jucePlugin/jucePlugin_artefacts/Release/AU/DSP56300Emu.component
7z a ../deploy/dsp56300emuTestConsoleMac.zip ../temp/cmake/Release/virusTestConsole ../deploy/linux/*.sh

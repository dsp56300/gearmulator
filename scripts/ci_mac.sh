git reset --hard
git pull --recurse-submodules
cd ../
./build_mac.sh
cd scripts
7z a ../deploy/VirusVSTMac.zip ../temp/cmake/Release/VirusVST2.vst
7z a ../deploy/VirusTestConsoleMac.zip ../temp/cmake/Release/virusTestConsole ../deploy/linux/*.sh

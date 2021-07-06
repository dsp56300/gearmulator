git reset --hard
git pull
cd ../
./build_mac.sh
cd scripts
7z a ../deploy/VirusVSTMac.zip ../temp/cmake/VirusVST2.vst
7z a ../deploy/VirusTestConsoleMac.zip ../temp/cmake/virusTestConsole ../deploy/linux/*.sh

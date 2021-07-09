cd ../
git pull
git submodule update --recursive
./build_linux.sh
cd scripts
7z a ../deploy/VirusVSTLinux.zip ../temp/cmake_linux/libVirusVST2.so
7z a ../deploy/VirusTestConsoleLinux.zip ../temp/cmake_linux/virusTestConsole ../deploy/linux/*.sh

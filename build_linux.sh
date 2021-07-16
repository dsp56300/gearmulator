cmake . -B ./temp/cmake_linux
cd ./temp/cmake_linux
cmake --build . --config Release
cpack -G DEB
cpack -G RPM
cpack -G ZIP
move *.deb ../../deploy/ 
move *.rpm ../../deploy/ 
move *.zip ../../deploy/ 

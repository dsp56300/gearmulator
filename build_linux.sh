cmake . -B ./temp/cmake_linux -Dgearmulator_BUILD_JUCEPLUGIN=ON -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=ON -Dgearmulator_BUILD_JUCEPLUGIN_LV2=ON -Dgearmulator_SYNTH_OSIRUS=ON
cd ./temp/cmake_linux
cmake --build . --config Release
cpack -G DEB
cpack -G RPM
cpack -G ZIP

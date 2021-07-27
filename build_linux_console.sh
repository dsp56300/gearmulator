cmake . -B ./temp/cmake_linux_console -Dgearmulator_BUILD_JUCEPLUGIN=OFF
cd ./temp/cmake_linux_console
cmake --build . --config Release

cmake . -B ./temp/cmake_linux_console -Dgearmulator_BUILD_JUCEPLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cd ./temp/cmake_linux_console
cmake --build . --config Release -j 4
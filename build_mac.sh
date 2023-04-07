cmake -G Xcode -S . -B ./temp/cmake
cd ./temp/cmake
cmake --build . --config Release --parallel 8
cpack -G ZIP

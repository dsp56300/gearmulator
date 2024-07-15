host=$(hostname)

if [ -z "$1" ]
then
	threads=4
else
	threads=$1
fi

cmake . -B ./temp/cmake_linux_${host} -Dgearmulator_BUILD_JUCEPLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cd ./temp/cmake_linux_${host}
cmake --build . --config Release -j $threads

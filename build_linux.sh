host=$(hostname)

if [ -z "$1" ]
then
	threads=4
else
	threads=$1
fi

cmake . -B ./temp/cmake_linux_${host} -Dgearmulator_BUILD_JUCEPLUGIN=ON -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=ON -Dgearmulator_BUILD_JUCEPLUGIN_LV2=ON -Dgearmulator_SYNTH_OSIRUS=ON
cd ./temp/cmake_linux_${host}
cmake --build . --config Release -j $threads
cpack -G DEB
cpack -G RPM
cpack -G ZIP

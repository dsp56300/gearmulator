cd ../
git reset --hard
git pull
git submodule update --recursive
./build_linux.sh
cd scripts

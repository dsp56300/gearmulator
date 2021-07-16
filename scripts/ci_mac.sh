cd ../
git reset --hard
git pull
git submodule update --recursive
./build_mac.sh
cd scripts

cd ../
git reset --hard
git pull
git submodule update --init --recursive
./build_mac.sh
cd scripts

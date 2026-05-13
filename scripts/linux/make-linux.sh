#!/usr/bin/env bash

make linux
cd build-linux
rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake Makefile libminhook.a libxdiff.a
mkdir -p ll-deps
unzip ../libs/linux_x86/Cg-3.1.zip -d ll-deps/
cp ../libs/linux_x86/libCg.so ll-deps/libCg2.so
cp ../libs/linux_x86/libopenal.so.0 ll-deps/libopenal.so.0
cp ../libs/linux_x86/libcrypto.so.0.9.7 ll-deps/libcrypto.so.0.9.7
cp ../libs/linux_x86/libssl.so.0.9.7 ll-deps/libssl.so.0.9.7
cp /usr/lib/i386-linux-gnu/libncurses.so.5 ll-deps/
cp /usr/lib/i386-linux-gnu/libpcsclite.so.1 ll-deps/
cp /usr/lib/i386-linux-gnu/libstdc++.so.5 ll-deps/
cp /usr/lib/i386-linux-gnu/libz.so.1 ll-deps/
mv libposixtime.so ll-deps/
mv libposixtime.so.1 ll-deps/
mv libposixtime.so.2.4 ll-deps/
mv libkswapapi.so ll-deps/
mv libsegaapi.so ll-deps/
mv linuxloader.so ll-deps/
cd ..
tar -czvf linuxloader-linux.tar.gz -C ./build-linux .
cd build-linux
mv ll-deps/libposixtime.so ./
mv ll-deps/libposixtime.so.1 ./
mv ll-deps/libposixtime.so.2.4 ./
mv ll-deps/libkswapapi.so ./
mv ll-deps/libsegaapi.so ./
mv ll-deps/linuxloader.so ./
rm -r ll-deps
cd ..
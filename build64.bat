@echo off
cd /d %~dp0

call "D:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvarsall.bat" x64
meson setup build64 --buildtype=release
ninja -C build64

mkdir bin
copy build64\scardreader.dll bin\scardreader.dll
#!/bin/bash
set -e

sed -i '/#define DXX_BUILD_DESCENT 2/d' ./common/include/dxxsconf.h

# record_sconf_results=false
scons d2x=gcc12,sdl2 gcc12_CXX=/usr/bin/g++-12 sdl2_sdl2=1
build/d2x-rebirth/d2x-rebirth -pilot Player -debug

echo "#define DXX_BUILD_DESCENT 2" >> ./common/include/dxxsconf.h

#!/bin/bash
set -e
scons d1x=gcc12,sdl2 d2x=gcc12,sdl2 gcc12_CXX=/usr/bin/g++-12 sdl2_sdl2=1
build/d1x-rebirth/d1x-rebirth -pilot Player

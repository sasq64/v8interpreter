#!/bin/sh

mkdir v8build
cd v8build
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH=`pwd`/depot_tools:$PATH
fetch v8
cd v8
gclient sync
make native -j8


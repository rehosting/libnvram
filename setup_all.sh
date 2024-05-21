#!/bin/bash

set -eux

rm -f *.so.*
./setup.sh arm
./setup.sh aarch64
./setup.sh mipsel
./setup.sh mipseb
./setup.sh mips64eb
rm -rf utils   || true
mkdir utils
mv *.so.* utils

mv utils/libnvram.so.arm utils/libnvram.so.armel

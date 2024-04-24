#!/bin/bash
set -eux

# Host is mapped at /app

rm -rf /app/out
mkdir /app/out

SCRATCH=$(mktemp -d)/libnvram
mkdir $SCRATCH

CC=arm-linux-musleabi-gcc make CFLAGS="-DCONFIG_ARM=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.armel
mv libnvram.so $SCRATCH/libnvram.so.armel
make clean

CC=mipsel-linux-musl-gcc make CFLAGS="-DCONFIG_MIPS=1 -march=mips32r2" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.mipsel
mv libnvram.so $SCRATCH/libnvram.so.mipsel
make clean

CC=mipseb-linux-musl-gcc make CFLAGS="-DCONFIG_MIPS=1 -march=mips32r2" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.mipseb
mv libnvram.so $SCRATCH/libnvram.so.mipseb
make clean

tar -czvf /app/libnvram-latest.tar.gz -C $(dirname $SCRATCH) libnvram
rm -rf /app/out

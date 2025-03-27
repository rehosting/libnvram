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

CC=arm-linux-musleabihf-gcc make CFLAGS="-DCONFIG_ARM=1 -mfloat-abi=hard" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.armelhf
mv libnvram.so $SCRATCH/libnvram.so.armelhf
make clean

CC=aarch64-linux-musl-gcc make CFLAGS="-DCONFIG_AARCH64=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.aarch64
mv libnvram.so $SCRATCH/libnvram.so.aarch64
make clean

CC=mipsel-linux-musl-gcc make CFLAGS="-DCONFIG_MIPS=1 -march=mips32r2" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.mipsel
mv libnvram.so $SCRATCH/libnvram.so.mipsel
make clean

CC=mipseb-linux-musl-gcc make CFLAGS="-DCONFIG_MIPS=1 -march=mips32r2" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.mipseb
mv libnvram.so $SCRATCH/libnvram.so.mipseb
make clean

CC=mips64eb-linux-musl-gcc make CFLAGS="-DCONFIG_MIPS=1 -march=mips64r2 -mabi=64" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.mips64eb
mv libnvram.so $SCRATCH/libnvram.so.mips64eb
make clean

CC=x86_64-linux-musl-gcc make CFLAGS="-DCONFIG_X86_64=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.x86_64
mv libnvram.so $SCRATCH/libnvram.so.x86_64
make clean

CC=i686-linux-musl-gcc make CFLAGS="-DCONFIG_I386=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.i386
mv libnvram.so $SCRATCH/libnvram.so.i386
make clean

CC=powerpc-linux-musl-gcc make CFLAGS="-DCONFIG_PPC=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.ppc
mv libnvram.so $SCRATCH/libnvram.so.ppc
make clean

CC=powerpcle-linux-musl-gcc make CFLAGS="-DCONFIG_PPC=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.ppcel
mv libnvram.so $SCRATCH/libnvram.so.ppcel
make clean

CC=powerpc64-linux-musl-gcc make CFLAGS="-DCONFIG_PPC64=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.ppc64
mv libnvram.so $SCRATCH/libnvram.so.ppc64
make clean

CC=powerpc64le-linux-musl-gcc make CFLAGS="-DCONFIG_PPC64=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.ppc64el
mv libnvram.so $SCRATCH/libnvram.so.ppc64el
make clean

CC=loongarch64-unknown-linux-gnu-gcc make CFLAGS="-DCONFIG_LOONGARCH64=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.loongarch64
mv libnvram.so $SCRATCH/libnvram.so.loongarch64
make clean

CC=riscv64-linux-musl-gcc make CFLAGS="-DCONFIG_RISCV64=1" libnvram.so -C /app
mv nvram.o $SCRATCH/nvram.o.riscv64
mv libnvram.so $SCRATCH/libnvram.so.riscv64
make clean

tar -czvf /app/libnvram-latest.tar.gz -C $(dirname $SCRATCH) libnvram
rm -rf /app/out

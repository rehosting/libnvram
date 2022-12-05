CC=gcc
CFLAGS=-O2 -fPIC -Wall
LDFLAGS=-shared -nostdlib

CXX=g++
CXXFLAGS=-O2 -fPIC -Wall -Wl,--no-undefined -g -D_GNU_SOURCE
#LXXFLAGS=-ldl -shared
LXXFLAGS=-shared -nostdlib

TARGET=libnvram.so libnvram_ioctl.so libvsockify.so

# /opt/cross/mips-linux-uclibc/mips-linux-uclibc/

all: $(SOURCES) $(TARGET)

libnvram.so: nvram.o
	$(CC) $(LDFLAGS) $< -o $@

libnvram_ioctl.so: nvram_ioctl.o
	$(CC) $(LDFLAGS) $< -o $@

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

nvram_ioctl.o: nvram.c
	$(CC) -c $(CFLAGS) -DFIRMAE_KERNEL $< -o $@

libvsockify.so: libvsockify.cpp
	$(CXX) $(CXXFLAGS)  -o $@ $^ $(LXXFLAGS)

clean:
	rm -f *.o libnvram.so libnvram_ioctl.so libvsockify.so

.PHONY: clean

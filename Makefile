CFLAGS=-O2 -fPIC -Wall
LDFLAGS=-shared -nostdlib
CXXFLAGS=-O2 -fPIC -Wl,--no-undefined -shared -g
CXX=g++

TARGET=libnvram.so libnvram_ioctl.so libvsockify.so

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
	$(CXX) $(CXXFLAGS)  -o $@ $^ -ldl -D_GNU_SOURCE

clean:
	rm -f *.o libnvram.so libnvram_ioctl.so

.PHONY: clean

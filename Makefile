CF := -O2 -fPIC -Wall -Wno-attribute-alias
CFLAGS :=
LDFLAGS := -shared -nostdlib

TARGET=libnvram.so libnvram_ioctl.so

all: $(SOURCES) $(TARGET)

libnvram.so: nvram.o
	$(CC) $(LDFLAGS) $< -o $@

libnvram_ioctl.so: nvram_ioctl.o
	$(CC) $(LDFLAGS) $< -o $@

.c.o:
	$(CC) -c $(CFLAGS) $(CF) $< -o $@

nvram_ioctl.o: nvram.c
	$(CC) -c $(CFLAGS) $(CF) -DFIRMAE_KERNEL $< -o $@

clean:
	rm -f *.o libnvram.so libnvram_ioctl.so

.PHONY: clean

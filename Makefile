CF := -O2 -fPIC -Wall -Wno-attribute-alias
CFLAGS :=
LDFLAGS := -shared -nostdlib

BASE := $(CC:-gcc=)
LIBCA := $(shell find /opt/cross/$(BASE)* -name 'libc.a')

TARGET=libnvram.so libnvram_ioctl.so

all: $(SOURCES) $(TARGET)

libnvram.so:
	$(CC) -c $(CFLAGS) $(CF) nvram.c -o nvram.o
	@./make_export_version.sh nvram.o /tmp/nvram.version
	$(CC) $(LDFLAGS) -Wl,--version-script=/tmp/nvram.version $<  $(LIBCA) -o $@

libnvram_ioctl.so: nvram_ioctl.o
	$(CC) $(LDFLAGS) $< -o $@

.c.o:
	$(CC) -c $(CFLAGS) $(CF) $< -o $@

nvram_ioctl.o: nvram.c
	$(CC) -c $(CFLAGS) $(CF) -DFIRMAE_KERNEL $< -o $@

clean:
	rm -f *.o libnvram.so libnvram_ioctl.so

.PHONY: clean

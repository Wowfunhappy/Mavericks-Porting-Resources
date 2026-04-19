CC = clang
CFLAGS = -Wall -Os -fPIC -fvisibility=default
PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib

# Each stub builds as a dylib with the name the runtime expects
all: libswiftCore.dylib libswiftFoundation.dylib libCryptoKit.dylib libswift_extras.dylib

libswiftCore.dylib: stub_swiftcore.c
	$(CC) $(CFLAGS) -dynamiclib -install_name @rpath/$@ -o $@ $<

libswiftFoundation.dylib: stub_swiftfoundation.c
	$(CC) $(CFLAGS) -dynamiclib -install_name @rpath/$@ -o $@ $<

libCryptoKit.dylib: stub_cryptokit.c
	$(CC) $(CFLAGS) -dynamiclib -install_name @rpath/$@ -o $@ $<

# Swift extras contains FORCE_LOAD symbols for all overlay dylibs.
# Install under multiple names so any overlay reference resolves.
libswift_extras.dylib: stub_swift_extras.c
	$(CC) $(CFLAGS) -dynamiclib -install_name @rpath/$@ -o $@ $<

install: all
	install -d $(LIBDIR)
	install -m 755 libswiftCore.dylib $(LIBDIR)
	install -m 755 libswiftFoundation.dylib $(LIBDIR)
	install -m 755 libCryptoKit.dylib $(LIBDIR)
	install -m 755 libswift_extras.dylib $(LIBDIR)

clean:
	rm -f libswiftCore.dylib libswiftFoundation.dylib libCryptoKit.dylib libswift_extras.dylib

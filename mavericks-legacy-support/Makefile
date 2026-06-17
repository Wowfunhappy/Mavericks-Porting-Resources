# MavericksLegacySupport - a minimal, single-target (OS X 10.9) polyfill library.
#
# Adding a polyfill never requires touching this Makefile: every src/*.c is
# compiled into the libraries automatically, and the whole include/ tree is
# installed as-is.
#
#   make            build the static and dynamic libraries (lib/)
#   make system-lib also build the libSystem-reexporting drop-in dylib
#   make test       build and run the test programs
#   make install    install headers + libraries under $(PREFIX)
#   make clean      remove build products

# ---- Configuration ---------------------------------------------------------

DESTDIR    ?=
PREFIX     ?= /usr/local
LIBDIR      = $(PREFIX)/lib
INCSUBDIR   = MavericksLegacySupport
INCDIR      = $(PREFIX)/include/$(INCSUBDIR)

CC         ?= cc
AR         ?= ar
INSTALL    ?= install

LIBNAME     = MavericksLegacySupport
SYSLIBNAME  = MavericksLegacySystem.B

# Build for the 10.9 deployment target regardless of host.
DEPTARGET   = -mmacosx-version-min=10.9

# Optional list of architectures, e.g. `make ARCHS="x86_64 i386"` for a fat
# build.  Empty means the compiler default (native).
ARCHS      ?=
ARCHFLAGS   = $(patsubst %,-arch %,$(ARCHS))

OPT        ?= -Os
WARN        = -Wall -Wno-deprecated-declarations
# -Iinclude lets our own sources pick up our wrapper headers (so a source and
# its declaration always agree).  -fPIC lets the same objects feed both the
# static archive and the dylib.
ALLCFLAGS   = $(ARCHFLAGS) $(DEPTARGET) $(OPT) $(WARN) -fPIC -Iinclude $(CFLAGS)
ALLLDFLAGS  = $(ARCHFLAGS) $(DEPTARGET) $(LDFLAGS)

# Frameworks needed by the wowfunhappy.c custom polyfills (Security/CF/objc, and
# CoreServices for the LaunchServices back-fill in launchservices.c).
# The dylibs link them directly; static-lib consumers must add these too.
WFHLIBS     = -framework CoreFoundation -framework Security -framework CoreServices -lobjc

# ---- Derived names ---------------------------------------------------------

SRCS       := $(wildcard src/*.c)
OBJS       := $(patsubst src/%.c,build/%.o,$(SRCS))

STATICLIB   = lib/lib$(LIBNAME).a
DYLIB       = lib/lib$(LIBNAME).dylib
SYSLIB      = lib/lib$(SYSLIBNAME).dylib

# ---- Top-level targets -----------------------------------------------------

.PHONY: all system-lib test clean install
.DEFAULT_GOAL := all

all: $(STATICLIB) $(DYLIB)

system-lib: $(SYSLIB)

# ---- Build rules -----------------------------------------------------------

build lib:
	mkdir -p $@

build/%.o: src/%.c | build
	$(CC) $(ALLCFLAGS) -c $< -o $@

$(STATICLIB): $(OBJS) | lib
	$(AR) -rcs $@ $(OBJS)

$(DYLIB): $(OBJS) | lib
	$(CC) $(ALLLDFLAGS) -dynamiclib \
	  -install_name $(LIBDIR)/lib$(LIBNAME).dylib \
	  -compatibility_version 1.0 -current_version 1.0 \
	  -o $@ $(OBJS) $(WFHLIBS)

# Drop-in replacement for libSystem.B.dylib: exports our additions *and*
# re-exports everything from the real libSystem, so it can stand in for it
# (e.g. via DYLD_INSERT_LIBRARIES or relinking).
$(SYSLIB): $(OBJS) | lib
	$(CC) $(ALLLDFLAGS) -dynamiclib -nodefaultlibs \
	  -install_name $(LIBDIR)/lib$(SYSLIBNAME).dylib \
	  -compatibility_version 1.0 -current_version 1.0 \
	  -Wl,-reexport_library,/usr/lib/libSystem.B.dylib \
	  -o $@ $(OBJS) $(WFHLIBS)

# ---- Tests -----------------------------------------------------------------

TESTSRCS := $(wildcard test/*.c)
TESTBINS := $(patsubst test/%.c,build/%,$(TESTSRCS))

test: $(TESTBINS)
	@echo "=== running tests ==="
	@for t in $(TESTBINS); do echo "--- $$t"; ./$$t || exit 1; done
	@echo "=== all tests passed ==="

build/%: test/%.c $(STATICLIB) | build
	$(CC) $(ARCHFLAGS) $(DEPTARGET) $(OPT) $(WARN) -Iinclude $< $(STATICLIB) $(WFHLIBS) -o $@

# ---- Install ---------------------------------------------------------------

install: all
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 644 $(STATICLIB) $(DYLIB) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -d $(DESTDIR)$(INCDIR)
	# Copy the whole header tree, preserving subdirectories.
	cp -R include/. $(DESTDIR)$(INCDIR)/

clean:
	rm -rf build lib

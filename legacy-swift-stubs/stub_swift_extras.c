/*
 * Stub Swift support dylibs for macOS 10.9 compatibility.
 * Provides _swift_FORCE_LOAD symbols and bridging functions.
 * Compiled once, installed under multiple names.
 */

#include <stddef.h>

/* FORCE_LOAD symbols - each Swift overlay dylib exports one of these.
 * The linker inserts references to force-load the containing dylib.
 * We define them all here so one stub satisfies every FORCE_LOAD. */

int _swift_FORCE_LOAD_$_swiftCoreFoundation = 0;
int _swift_FORCE_LOAD_$_swiftDarwin = 0;
int _swift_FORCE_LOAD_$_swiftDispatch = 0;
int _swift_FORCE_LOAD_$_swiftFoundation = 0;
int _swift_FORCE_LOAD_$_swiftIOKit = 0;
int _swift_FORCE_LOAD_$_swiftObjectiveC = 0;
int _swift_FORCE_LOAD_$_swiftXPC = 0;
int _swift_FORCE_LOAD_$_swift_Builtin_float = 0;
int _swift_FORCE_LOAD_$_swift_errno = 0;
int _swift_FORCE_LOAD_$_swift_math = 0;
int _swift_FORCE_LOAD_$_swift_signal = 0;
int _swift_FORCE_LOAD_$_swift_stdio = 0;
int _swift_FORCE_LOAD_$_swift_time = 0;
int _swift_FORCE_LOAD_$_swiftsys_time = 0;
int _swift_FORCE_LOAD_$_swiftunistd = 0;

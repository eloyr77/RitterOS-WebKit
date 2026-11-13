# The RitterOS port (Compass, the browser for RitterOS): a POSIX system with
# no GLib, built with RitterOS's SDK toolchain (sdk/ritteros.cmake, through
# Compass's cmake/compass-toolchain.cmake). Like the Win port it draws with
# Skia on the CPU and fetches with curl and OpenSSL (RitterOS's system
# libraries); like the PlayStation port it is Unix underneath without Linux.
#
# RITTEROS_JSC_ONLY=ON builds WTF, bmalloc and JavaScriptCore (the jsc shell)
# alone, the port's first step.

find_package(Threads REQUIRED)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# The compiler's target is ARCH-unknown-none-elf, which predefines nothing
# for the system: WTF's PlatformOS.h takes OS(RITTEROS) (and OS(UNIX)) from
# __ritteros__.
add_definitions(-D__ritteros__=1 -DBUILDING_RITTEROS__=1)
# (Until RitterOS's <string.h> declares strcasecmp and strncasecmp, as glibc's
# and the BSDs' do: Compass's docs/os-requests.md, 1.)
add_compile_options("SHELL:-include strings.h")

# The SDK's toolchain links with ld.lld itself, not through clang: CMake's
# LINKER: options go to it as they are, not behind -Xlinker.
foreach (_lang C CXX)
    set(CMAKE_${_lang}_LINKER_WRAPPER_FLAG "")
    set(CMAKE_${_lang}_LINKER_WRAPPER_FLAG_SEP "")
endforeach ()

set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_MICRO 0)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_MICRO})

set(RITTEROS_JSC_ONLY ON CACHE BOOL "Build only WTF, bmalloc and JavaScriptCore")

WEBKIT_OPTION_BEGIN()
# JavaScript: LLInt, the interpreter in assembly, without a JIT for now
# (RitterOS keeps W^X: a JIT needs JSC's dual-mapped executable memory, 45d).
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_JIT PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_DFG_JIT PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_FTL_JIT PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_C_LOOP PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBASSEMBLY PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_SAMPLING_PROFILER PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_REMOTE_INSPECTOR PRIVATE OFF)
# The system's malloc (no libpas/Gigacage for a start).
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_SYSTEM_MALLOC PRIVATE ON)
WEBKIT_OPTION_END()

SET_AND_EXPOSE_TO_BUILD(USE_GENERIC_EVENT_LOOP 1)
SET_AND_EXPOSE_TO_BUILD(WTF_DEFAULT_EVENT_LOOP 0)
set(EVENT_LOOP_TYPE "Generic")
set(LOWERCASE_EVENT_LOOP_TYPE "generic")

if (RITTEROS_JSC_ONLY)
    set(ENABLE_WEBCORE OFF)
    set(ENABLE_WEBKIT_LEGACY OFF)
    set(ENABLE_WEBKIT OFF)
    set(ENABLE_WEBINSPECTORUI OFF)
    set(ENABLE_WEBGL OFF)
    set(ENABLE_WEBGPU OFF)
endif ()
set(ENABLE_API_TESTS OFF)

set(JavaScriptCore_LIBRARY_TYPE SHARED)
set(bmalloc_LIBRARY_TYPE OBJECT)
set(WTF_LIBRARY_TYPE OBJECT)

# ICU is Compass's own, static (sysroot/ARCH: deps/build.sh).
find_package(ICU 70.1 REQUIRED COMPONENTS data i18n uc)
add_definitions(-DU_STATIC_IMPLEMENTATION=1)

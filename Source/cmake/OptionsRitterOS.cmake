# extending the JSC-only file at upstream b09e16e32 + 699d9ef77 (branch ritteros).
# See build/port-design/REPORT.md for the reasoning and the evidence behind each line.
#
# The RitterOS port (Compass, the browser for RitterOS): a POSIX system with
# no GLib and no GPU, built with RitterOS's SDK toolchain (sdk/ritteros.cmake, through
# Compass's cmake/compass-toolchain.cmake). Like the Win port it draws with
# Skia on the CPU and fetches with curl and OpenSSL; like the PlayStation port it
# is Unix underneath without Linux, and paints with the non-composited
# DrawingAreaCoordinatedGraphics/UpdateInfo/BackingStore path
# (AcceleratedCompositingEnabled=false). Nothing compiled includes a GL, EGL,
# epoxy, ANGLE or Vulkan header.
#
# RITTEROS_JSC_ONLY=ON builds WTF, bmalloc and JavaScriptCore (the jsc shell) alone.

find_package(Threads REQUIRED)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# The compiler's target is ARCH-unknown-none-elf, which predefines nothing
# for the system: WTF's PlatformOS.h takes OS(RITTEROS) (and OS(UNIX)) from
# __ritteros__. BUILDING_RITTEROS__ gives PLATFORM(RITTEROS) once
# wtf/PlatformLegacy.h maps it (REPORT.md, patch W1).
add_definitions(-D__ritteros__=1 -DBUILDING_RITTEROS__=1)
# Libraries and programs without debug sections (libWebKit is 114 MB with
# them); the symbol tables stay, for backtraces (lldb "image lookup -a").
add_link_options(--strip-debug)

set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_MICRO 0)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_MICRO})

set(RITTEROS_JSC_ONLY OFF CACHE BOOL "Build only WTF, bmalloc and JavaScriptCore")

# Cross builds already default these to OFF (WebKitFeatures.cmake:162-176 tests
# CMAKE_CROSSCOMPILING); pin them so a host swiftc is never probed.
set(ENABLE_BACK_FORWARD_LIST_SWIFT_DEFAULT OFF)
set(ENABLE_SWIFT_BASE_CLASS_ANNOTATIONS_DEFAULT OFF)

# -----------------------------------------------------------------------------
# Libraries. ICU, FreeType, HarfBuzz, Fontconfig, expat, libpng, libjpeg-turbo,
# libwebp(+demux, sharpyuv), libxml2, sqlite3 and woff2 are Compass's own static
# builds in sysroot/ARCH (deps/build.sh); curl, OpenSSL, zlib, libpsl, brotli and
# nghttp2 are RitterOS's system libraries.
# -----------------------------------------------------------------------------
find_package(ICU 70.1 REQUIRED COMPONENTS data i18n uc)
add_definitions(-DU_STATIC_IMPLEMENTATION=1)

if (NOT RITTEROS_JSC_ONLY)
    find_package(ZLIB 1.2.11 REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(CURL 7.87.0 REQUIRED)        # >= 7.66 for curl_multi_poll (CurlContext.cpp:274)
    find_package(LibPSL 0.20.2 REQUIRED)      # Source/cmake/FindLibPSL.cmake (pkg-config libpsl)
    find_package(LibXml2 2.9.7 REQUIRED)
    find_package(SQLite3 3.23.1 REQUIRED)
    if (NOT TARGET SQLite3::SQLite3) # CMake < 4.3
        add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
    endif ()
    find_package(PNG 1.6.34 REQUIRED)
    find_package(JPEG 1.5.2 REQUIRED)
    find_package(WebP REQUIRED COMPONENTS demux)
    find_package(Freetype 2.9.0 REQUIRED)     # Skia's CMakeLists.txt:4-7 finds these again
    find_package(Fontconfig 2.13.0 REQUIRED)
    find_package(HarfBuzz 1.4.2 REQUIRED COMPONENTS ICU)
    find_package(EXPAT REQUIRED)              # only to complete static Fontconfig's link
    find_library(SHARPYUV_LIBS NAMES sharpyuv libsharpyuv)  # as OptionsWin.cmake:171

    # The sysroot's libraries are static and CMake's find modules do not carry
    # their pkg-config Requires (fontconfig.pc: freetype2 expat; freetype2.pc:
    # zlib libpng; harfbuzz.pc: freetype2; libwebp.pc: libsharpyuv).
    set_property(TARGET Freetype::Freetype APPEND PROPERTY INTERFACE_LINK_LIBRARIES PNG::PNG ZLIB::ZLIB)
    set_property(TARGET Fontconfig::Fontconfig APPEND PROPERTY INTERFACE_LINK_LIBRARIES Freetype::Freetype EXPAT::EXPAT)
    set_property(TARGET HarfBuzz::HarfBuzz APPEND PROPERTY INTERFACE_LINK_LIBRARIES Freetype::Freetype)
    if (SHARPYUV_LIBS)
        set_property(TARGET WebP::libwebp APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${SHARPYUV_LIBS})
    endif ()
    # A static libcurl needs OpenSSL, zlib, libpsl, brotli and nghttp2 behind it. Prefer
    # curl's own CURLConfig.cmake (it carries them) if the SDK installs one; otherwise:
    #   pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)
    #   set_property(TARGET CURL::libcurl APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    #       OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB LibPSL::LibPSL Brotli::dec PkgConfig::NGHTTP2)
endif ()

WEBKIT_OPTION_BEGIN()

# JavaScript: LLInt, then the JITs (baseline, DFG). RitterOS keeps W^X:
# JIT code is written through a second mapping of its memory (45d;
# ENABLE_SEPARATED_WX_HEAP, ExecutableAllocator.cpp). JavaScriptCoreUseJIT=0
# in the environment runs the interpreter alone. The FTL is off: its B3
# code uses WebAssembly's types and did not build without it; WebAssembly
# is on now, and the FTL not yet tried.
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_JIT PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_DFG_JIT PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_FTL_JIT PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_C_LOOP PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBASSEMBLY PRIVATE ON)  # its interpreter (IPInt): BBQ/OMG wait for the FTL
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_SAMPLING_PROFILER PRIVATE OFF)
# The system's malloc (no libpas/Gigacage for a start).
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_SYSTEM_MALLOC PRIVATE ON)

# Graphics: Skia on the CPU (WebKitFeatures.cmake:361 defaults USE_SKIA OFF).
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_SKIA PRIVATE ON)
# Skia's encoders need libwebpmux (skia/CMakeLists.txt:870-881): the sysroot has it.
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_SKIA_ENCODERS PRIVATE ON)   # canvas toDataURL/toBlob
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_WOFF2 PRIVATE ON)           # woff2dec + brotlidec
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VARIATION_FONTS PRIVATE ON)

# Defaults (WebKitFeatures.cmake:205-366) that would need libraries RitterOS has not got.
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_AVIF PRIVATE OFF)           # default ON: libavif
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_JPEGXL PRIVATE OFF)         # default ON: libjxl
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_LCMS PRIVATE OFF)           # default ON: lcms2
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_XSLT PRIVATE OFF)        # default ON: libxslt
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBGL PRIVATE OFF)       # default ON: ANGLE (Source/CMakeLists.txt:30)
# <video> and <audio> exist (sites such as YouTube stop at a missing
# HTMLVideoElement) but have no media engine yet: MediaPlayer's engine list is
# empty, so nothing plays and canPlayType answers "".
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEB_AUDIO PRIVATE ON)    # platform/audio/ritteros: soundd, FFmpeg
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_REMOTE_INSPECTOR PRIVATE OFF) # default ON
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_GEOLOCATION PRIVATE ON)  # no position source: requests are refused
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_NOTIFICATIONS PRIVATE ON) # permission is refused (no notification service)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_IMAGE_DIFF PRIVATE OFF)  # default ON (Tools/ImageDiff)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_CONTEXT_MENUS PRIVATE OFF) # default ON; needs a WebContextMenuProxy (M2)
# The Fullscreen API: the view client's fullscreen steps (RitterOSWebView,
# WKView.cpp); Compass gives the page the whole screen (window_fullscreen).
# A video's own fullscreen button goes through it too.
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_FULLSCREEN_API PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO_USES_ELEMENT_FULLSCREEN PRIVATE ON)

# Milestone 1: everything that is not "load and render http(s) pages in a window".
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_GPU_PROCESS PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBGPU PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBXR PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_STREAM PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_RECORDER PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_SOURCE PRIVATE ON)  # (YouTube plays through it)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_SESSION PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_ENCRYPTED_MEDIA PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEB_CODECS PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEB_RTC PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_SPEECH_SYNTHESIS PRIVATE ON) # no voices (PlatformSpeechSynthesizerRitterOS.cpp)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_GAMEPAD PRIVATE ON)    # no gamepads (WebCore's EmptyGamepadProvider)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_SPELLCHECK PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_TOUCH_EVENTS PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_DRAG_SUPPORT PRIVATE OFF)  # also mouse-drag text selection: M2
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_OFFSCREEN_CANVAS PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_OFFSCREEN_CANVAS_IN_WORKERS PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_PDFJS PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBDRIVER PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MINIBROWSER PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_LAYOUT_TESTS PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_API_TESTS PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_RESOURCE_USAGE PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_BUBBLEWRAP_SANDBOX PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_BREAKPAD PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_APPLICATION_MANIFEST PRIVATE OFF)
# No ScrollingThread: the non-composited drawing area scrolls by copying pixels
# (UpdateInfo::scrollRect), as OptionsPlayStation.cmake:217 does.
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_ASYNC_SCROLLING PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_SMOOTH_SCROLLING PRIVATE ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_BACK_FORWARD_LIST_SWIFT PRIVATE OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_IPC_TESTING_SWIFT PRIVATE OFF)

WEBKIT_OPTION_END()

SET_AND_EXPOSE_TO_BUILD(ENABLE_DEVELOPER_MODE ${DEVELOPER_MODE})

# Event loop: WTF's generic RunLoop (no GLib). setWakeUpCallback()/cycle() let an
# embedder fold it into its own loop (wtf/generic/RunLoopGeneric.cpp:240,277).
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
else ()
    set(ENABLE_WEBCORE ON)
    set(ENABLE_WEBKIT ON)
    set(ENABLE_WEBKIT_LEGACY OFF)
    set(ENABLE_WEBINSPECTORUI OFF)

    # IPC: Unix domain sockets (SOCK_SEQPACKET) with SCM_RIGHTS, shared memory
    # by fd (PlayStation sets the same, OptionsPlayStation.cmake:268; PlatformUse.h:167
    # only defines it for GTK/WPE).
    SET_AND_EXPOSE_TO_BUILD(USE_UNIX_DOMAIN_SOCKETS ON)

    # Networking: curl + OpenSSL.
    SET_AND_EXPOSE_TO_BUILD(USE_CURL ON)
    SET_AND_EXPOSE_TO_BUILD(USE_OPENSSL ON)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
    set(CMAKE_REQUIRED_LIBRARIES "${OPENSSL_LIBRARIES}")
    WEBKIT_CHECK_HAVE_SYMBOL(USE_BORINGSSL OPENSSL_IS_BORINGSSL openssl/ssl.h)
    cmake_pop_check_state()

    # Text: HarfBuzz (PlatformUse.h:78 only defines it for GTK/WPE).
    SET_AND_EXPOSE_TO_BUILD(USE_HARFBUZZ ON)

    # Form controls and scrollbars drawn by WebCore itself (as Win/GTK/WPE).
    SET_AND_EXPOSE_TO_BUILD(USE_THEME_ADWAITA ON)

    # Rendering. USE_TEXTURE_MAPPER is ON only as the switch that selects the
    # UpdateInfo/BackingStore protocol and DrawingAreaCoordinatedGraphics
    # (DrawingArea.cpp:46-71, DrawingArea.messages.in:28-38,
    # DrawingAreaProxy.messages.in:35-38, UpdateInfo.h:28). WebCore's
    # platform/TextureMapper.cmake is NOT included (PlatformRitterOS.cmake), so
    # TextureMapper.cpp/TextureMapperGLHeaders.h never compile.
    SET_AND_EXPOSE_TO_BUILD(USE_TEXTURE_MAPPER ON)
    SET_AND_EXPOSE_TO_BUILD(USE_COORDINATED_GRAPHICS OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_GRAPHICS_LAYER_TEXTURE_MAPPER OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_GRAPHICS_LAYER_WC OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_CAIRO OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_ANGLE OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_LIBEPOXY OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_GBM OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_LIBDRM OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_LIBWPE OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_WPE_RENDERER OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_XDGMIME OFF)
    SET_AND_EXPOSE_TO_BUILD(HAVE_DISPLAY_LINK OFF)
    SET_AND_EXPOSE_TO_BUILD(HAVE_GL_FENCE OFF)
    SET_AND_EXPOSE_TO_BUILD(USE_INSPECTOR_SOCKET_SERVER OFF)
    set(USE_ANGLE_EGL OFF)

    # Shared memory: SharedMemoryUnix.cpp only knows Linux's raw syscall
    # (HAVE_LINUX_MEMFD_H, 0 in this build) or shm_open; RitterOS has memfd_create().
    # Patch WC5 in REPORT.md consumes this.
    WEBKIT_CHECK_HAVE_FUNCTION(HAVE_MEMFD_CREATE memfd_create sys/mman.h)

    if (USE_WOFF2)
        find_package(WOFF2 1.0.2 REQUIRED COMPONENTS dec)
        find_package(Brotli REQUIRED COMPONENTS dec)
    endif ()

    # Where the UI process finds WebKitWebProcess/WebKitNetworkProcess
    # (ProcessLauncherRitterOS.cpp; RITTEROS_WEBKIT_EXEC_PATH overrides it at run time).
    set(RITTEROS_LIBEXECDIR "/system/libexec/compass" CACHE PATH "Install directory of the WebKit auxiliary processes")
    EXPOSE_STRING_VARIABLE_TO_BUILD(RITTEROS_LIBEXECDIR)
endif ()

set(ENABLE_API_TESTS OFF)
set(ENABLE_TOOLS OFF)

# Libraries as PlayStation builds them (OptionsPlayStation.cmake:359-374): one shared
# libJavaScriptCore with WTF and bmalloc inside, one shared libWebKit with PAL and
# WebCore inside, and the WebKitWebProcess/WebKitNetworkProcess executables.
set(bmalloc_LIBRARY_TYPE OBJECT)
set(WTF_LIBRARY_TYPE OBJECT)
set(JavaScriptCore_LIBRARY_TYPE SHARED)
set(PAL_LIBRARY_TYPE OBJECT)
set(WebCore_LIBRARY_TYPE OBJECT)
set(WebKit_LIBRARY_TYPE SHARED)

# Source/WebKit/PlatformRitterOS.cmake: the RitterOS port's WebKit (docs/webkit-port.md in Compass)
#
# Borrowed from PlatformPlayStation.cmake (the non-GLib Unix process model, IPC/unix,
# Shared/unix/AuxiliaryProcessMain.cpp, DrawingAreaCoordinatedGraphics +
# DrawingAreaProxyCoordinatedGraphics + UIProcess/skia/BackingStoreSkia.cpp, the
# C-API view) minus everything libwpe/WPE-backend/GPU-process/EGL, and from
# PlatformWin.cmake for the Skia-without-GL pieces. Not Platform/WC.cmake (that
# is the GPU-process DrawingAreaWC path). See REPORT.md, sections 1, 2 and 4.

include(Headers.cmake)
include(Platform/Curl.cmake)   # NetworkProcess/curl/*, Cookies/curl, cache/*Curl, WebsiteDataStoreCurl, ...
include(Platform/Skia.cmake)   # WKImageSkia, WebAutomationSessionSkia, Skia IPC serializers

set(WebProcess_OUTPUT_NAME WebKitWebProcess)
set(NetworkProcess_OUTPUT_NAME WebKitNetworkProcess)

# Entry points: plain main() -> WebKit::WebProcessMain / NetworkProcessMain.
list(APPEND WebProcess_SOURCES
    WebProcess/EntryPoint/unix/WebProcessMain.cpp
)
list(APPEND NetworkProcess_SOURCES
    NetworkProcess/EntryPoint/unix/NetworkProcessMain.cpp
)
# abort() and faults print a backtrace (WTF/wtf/ritteros/AbortBacktraceRitterOS.cpp).
list(APPEND WebProcess_SOURCES ${WTF_DIR}/wtf/ritteros/AbortBacktraceRitterOS.cpp)
list(APPEND NetworkProcess_SOURCES ${WTF_DIR}/wtf/ritteros/AbortBacktraceRitterOS.cpp)
# With COMPASS_PROFILE=FOLDER, the web process's main thread is sampled
# there (WTF/wtf/ritteros/ProfilerRitterOS.cpp, started by WebProcessMain,
# which is libWebKit's).
list(APPEND WebKit_SOURCES ${WTF_DIR}/wtf/ritteros/ProfilerRitterOS.cpp)
cmake_language(DEFER CALL target_link_options WebProcess PRIVATE "--export-dynamic-symbol=abort")
cmake_language(DEFER CALL target_link_options NetworkProcess PRIVATE "--export-dynamic-symbol=abort")
list(APPEND WebProcess_PRIVATE_LIBRARIES OpenSSL::Crypto)
list(APPEND NetworkProcess_PRIVATE_LIBRARIES OpenSSL::Crypto)

list(APPEND WebKit_SOURCES
    NetworkProcess/NetworkDataTaskDataURL.cpp
    NetworkProcess/Classifier/WebResourceLoadStatisticsStore.cpp

    # IPC over AF_UNIX/SOCK_SEQPACKET + SCM_RIGHTS. ConnectionUnix.cpp needs patch WK5
    # (the PlayStation socket-monitor thread for PLATFORM(RITTEROS)).
    Platform/IPC/unix/ArgumentCodersUnix.cpp
    Platform/IPC/unix/ConnectionUnix.cpp
    Platform/IPC/unix/IPCSemaphoreUnix.cpp      # non-Linux: inert stub; only GPU-process streams use it
    Platform/IPC/unix/IPCUtilitiesUnix.cpp

    Platform/classifier/ResourceLoadStatisticsClassifier.cpp

    Platform/unix/LoggingUnix.cpp
    Platform/unix/ModuleUnix.cpp                # load() returns false: no injected bundles in M1

    Shared/unix/AuxiliaryProcessMain.cpp        # argv: <processIdentifier> <socket fd>

    UIProcess/DefaultUndoController.cpp
    UIProcess/LegacySessionStateCodingNone.cpp
    UIProcess/WebGrammarDetail.cpp
    UIProcess/WebMemoryPressureHandler.cpp

    UIProcess/API/C/WKUserScriptRef.cpp

    # Non-composited drawing: WebProcess paints into a ShareableBitmap (SharedMemory
    # fd), sends UpdateInfo; the UI process keeps a Skia raster BackingStore.
    UIProcess/CoordinatedGraphics/DrawingAreaProxyCoordinatedGraphics.cpp
    UIProcess/skia/BackingStoreSkia.cpp
    WebProcess/WebPage/CoordinatedGraphics/DrawingAreaCoordinatedGraphics.cpp  # + patch WK3 (LayerTreeHostNull.h)

    # --- New RitterOS files (each modeled on the file named after it) ---


    UIProcess/API/C/ritteros/WKRunLoop.cpp                  # UIProcess/API/C/playstation/WKRunLoop.cpp (verbatim)
    UIProcess/API/C/ritteros/WKView.cpp                     # UIProcess/API/C/playstation/WKView.cpp + WKPagePrivatePlayStation.cpp
    UIProcess/API/C/ritteros/WKUserContentRitterOS.cpp      # user scripts, message handlers, callAsyncJavaScript in a named world

    UIProcess/Launcher/ritteros/ProcessLauncherRitterOS.cpp # NEW: posix_spawn (draft in build/port-design/src/)

    UIProcess/WebsiteData/ritteros/WebsiteDataStoreRitterOS.cpp # UIProcess/WebsiteData/playstation/WebsiteDataStorePlayStation.cpp

    UIProcess/ritteros/PageClientImpl.cpp                   # UIProcess/playstation/PageClientImpl.cpp (no WC/WPE)
    UIProcess/ritteros/RitterOSWebView.cpp                  # UIProcess/playstation/PlayStationWebView.cpp (+ paint, setSize -> drawingArea)
    UIProcess/ritteros/WebPageProxyRitterOS.cpp             # UIProcess/playstation/WebPageProxyPlayStation.cpp (no viewBackend())
    # (no WebPasteboardProxy file: WebPasteboardProxy.cpp:115-122 already stubs the non-libwpe, non-Win case)
    UIProcess/ritteros/WebPreferencesRitterOS.cpp           # UIProcess/playstation/WebPreferencesPlayStation.cpp (AC off) + patch WK2
    UIProcess/ritteros/WebProcessPoolRitterOS.cpp           # UIProcess/playstation/WebProcessPoolPlayStation.cpp (no userId)

    WebProcess/InjectedBundle/ritteros/InjectedBundleRitterOS.cpp # WebProcess/InjectedBundle/playstation/InjectedBundlePlayStation.cpp (returns false)

    WebProcess/WebPage/ritteros/WebPageRitterOS.cpp         # WebProcess/WebPage/playstation/WebPagePlayStation.cpp

    WebProcess/ritteros/WebProcessMainRitterOS.cpp          # WebProcess/playstation/WebProcessMainPlayStation.cpp (SkGraphics::Init)
    WebProcess/ritteros/WebProcessRitterOS.cpp              # WebProcess/playstation/WebProcessPlayStation.cpp (no PlatformDisplayLibWPE)
)

list(APPEND WebKit_PRIVATE_INCLUDE_DIRECTORIES
    "${WEBKIT_DIR}/Platform/IPC/unix"
    "${WEBKIT_DIR}/Platform/classifier"
    "${WEBKIT_DIR}/Platform/generic"
    "${WEBKIT_DIR}/Shared/API/c/ritteros"
    "${WEBKIT_DIR}/Shared/ritteros"
    "${WEBKIT_DIR}/Shared/unix"
    "${WEBKIT_DIR}/UIProcess/API/C/ritteros"
    "${WEBKIT_DIR}/UIProcess/API/ritteros"
    "${WEBKIT_DIR}/UIProcess/CoordinatedGraphics"
    "${WEBKIT_DIR}/UIProcess/ritteros"
    "${WEBKIT_DIR}/WebProcess/WebPage/CoordinatedGraphics"
)

list(APPEND WebKit_PUBLIC_FRAMEWORK_HEADERS
    Shared/API/c/ritteros/WKBaseRitterOS.h      # typedef const struct OpaqueWKView* WKViewRef; (patch WK7 in WKBase.h)

    UIProcess/API/C/ritteros/WKRunLoop.h
    UIProcess/API/C/ritteros/WKView.h           # draft in build/port-design/src/WKView.h
    UIProcess/API/C/ritteros/WKViewClient.h
    UIProcess/API/C/ritteros/WKUserContentRitterOS.h
)

if (ENABLE_SPEECH_SYNTHESIS)
    list(APPEND WebKit_SOURCES
        UIProcess/gstreamer/WebPageProxyGStreamer.cpp       # the UI process's speech callbacks (no GStreamer in it)
    )
endif ()

# WebKit/CMakeLists.txt:829-845 adds ${EGL_INCLUDE_DIRS}/${EGL_LIBRARIES}/${EGL_DEFINITIONS}
# when neither USE_LIBEPOXY nor USE_ANGLE_EGL: those are empty here (nobody runs
# find_package(EGL)), so there is nothing to undo.

# Not compiled (each pulls GL/EGL or the GPU process):
#   WebProcess/WebPage/CoordinatedGraphics/{AcceleratedSurface*,LayerTreeHost*,ThreadedCompositor*,
#     CompositingRunLoop,CoordinatedSceneState,NonCompositedFrameRenderer,FrameRenderer,
#     DrawingAreaCoordinatedGraphicsGLib}.cpp
#   Platform/WC.cmake (DrawingAreaWC, GraphicsLayerWC, RemoteWCLayerTreeHost, ...)
#   GPUProcess/* platform files, UIProcess/playstation/DisplayLinkPlayStation.cpp (HAVE(DISPLAY_LINK) is off)

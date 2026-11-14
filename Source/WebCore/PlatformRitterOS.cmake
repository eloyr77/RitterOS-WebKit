# DRAFT: Source/WebCore/PlatformRitterOS.cmake
#
# Borrowed from: PlatformPlayStation.cmake (non-GLib Unix, curl, OpenSSL, the
# non-composited drawing path), PlatformWin.cmake (Skia + Adwaita + curl without
# GLib), PlatformWPE.cmake / SourcesWPE.txt (Skia's FreeType/Fontconfig font path,
# platform/unix). See build/port-design/REPORT.md, section 2.
#
# Deliberately NOT included (they compile GL/EGL/epoxy):
#   platform/TextureMapper.cmake      TextureMapper.h -> TextureMapperGLHeaders.h -> <GLES2/gl2.h>
#   platform/CoordinatedGraphics.cmake
#   platform/graphics/PlatformDisplay.cpp, platform/graphics/egl/*.cpp (GLContext.cpp, GLDisplay.cpp,
#   GLFence*.cpp, BitmapTexture*.cpp, GraphicsContextGLEGL.cpp), platform/graphics/libwpe/*,
#   platform/graphics/angle/*
#   platform/FreeType.cmake (that is the Cairo font path; Skia has its own)

include(platform/Adwaita.cmake)
include(platform/Curl.cmake)
include(platform/ImageDecoders.cmake)
include(platform/OpenSSL.cmake)
include(platform/Skia.cmake)

# WebCore/CMakeLists.txt:2303-2320 links OpenGL::GLES (and EGL) whenever neither
# USE_ANGLE_EGL nor USE_LIBEPOXY is set. This file is included after that
# (CMakeLists.txt:2930) and before WEBKIT_FRAMEWORK(WebCore) (:3394), so take it back out
# instead of patching CMakeLists.txt.
list(REMOVE_ITEM WebCore_LIBRARIES OpenGL::GLES)

# platform/SourcesSkia.txt lists GPU-only Skia files. WEBKIT_COMPUTE_SOURCES filters
# unified-source list lines through <framework>_UNIFIED_SOURCE_EXCLUDES
# (cmake/WebKitMacros.cmake:120-135).
list(APPEND WebCore_UNIFIED_SOURCE_EXCLUDES
    "platform/MediaSamplesDecoder\\.cpp"                            # need ENABLE(VIDEO)
    "platform/PacedMediaSamplesDecoder\\.cpp"
    "platform/VideoDecoder\\.cpp"
    "platform/graphics/skia/GraphicsContextGLSkia\\.cpp"            # WebGL
    "platform/graphics/skia/ImageBufferSkiaAcceleratedBackend\\.cpp" # Ganesh/GL surfaces
    "platform/graphics/skia/PlatformDisplaySkia\\.cpp"               # <EGL/egl.h>, <epoxy/egl.h>
    "platform/graphics/skia/SkiaGPUAtlas\\.cpp"                      # BitmapTexture, GLFence
    "platform/graphics/skia/SkiaReplayAtlas\\.cpp"                   # GPU atlases
    # Only ImageBufferSkiaAcceleratedBackend.cpp and SkiaPaintingEngine.cpp create
    # SkiaRecordingResult / SkiaReplayCanvas; their destructors would need ~SkiaGPUAtlas().
    "platform/graphics/skia/SkiaRecordingResult\\.cpp"
    "platform/graphics/skia/SkiaReplayCanvas\\.cpp"
    # USE(COORDINATED_GRAPHICS)-only: they compile to nothing, drop them anyway.
    "platform/graphics/skia/SkiaBackingStore\\.cpp"
    "platform/graphics/skia/SkiaCompositingLayer"
    "platform/graphics/skia/SkiaPaintingEngine\\.cpp"
)
# (Files that stay and still reference the GL/Ganesh layer need USE(SKIA_GPU) guards:
#  ImageBuffer.cpp, GraphicsContextSkia.cpp, NativeImageSkia.cpp, ImageUtilitiesSkia.cpp,
#  SkiaSerializedImageBuffer.cpp, SkiaUtilities.cpp: REPORT.md patch list WC1-WC4.)

list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
    # Headers only. GraphicsContextSkia.h -> SkiaRecordingResult.h -> GLFence.h, SkiaGPUAtlas.h ->
    # BitmapTexture.h (egl/) -> ClipStack.h (texmap/, under USE(TEXTURE_MAPPER)). None of these
    # includes a system GL/EGL header (GLDisplay.h typedefs the EGL types itself, :32-37);
    # GLContext.h does (<EGL/eglplatform.h>, :28-33), hence patch WC0.
    "${WEBCORE_DIR}/platform/graphics/egl"
    "${WEBCORE_DIR}/platform/graphics/texmap"

    "${WEBCORE_DIR}/platform/generic"
    "${WEBCORE_DIR}/platform/graphics/opentype"
    "${WEBCORE_DIR}/platform/graphics/ritteros"
    "${WEBCORE_DIR}/platform/mediacapabilities"
    "${WEBCORE_DIR}/platform/network/ritteros"
    "${WEBCORE_DIR}/platform/ritteros"
    "${WEBCORE_DIR}/platform/text/icu"
    "${WEBCORE_DIR}/platform/unix"
    "${WEBCORE_DIR}/platform/video-codecs"
)

# egl/*.h are always copied (Headers.cmake:2713-2719); ClipStack.h normally comes from
# platform/TextureMapper.cmake:22, which is not included. WebKit needs it too
# (BackingStoreSkia.cpp -> GraphicsContextSkia.h -> ... -> BitmapTexture.h:37-40).
list(APPEND WebCore_PRIVATE_FRAMEWORK_HEADERS
    platform/graphics/texmap/ClipStack.h
)

list(APPEND WebCore_SOURCES
    # Accessibility: no AT bridge. Reuse PlayStation's stubs (Haiku already does,
    # PlatformHaiku.cmake:9-10) once PLATFORM(RITTEROS) joins PLAYSTATION/HAIKU in
    # ChromeClient.h:641, EmptyClients.h:177, AXCoreObject.h:84,1538 (patch WC6).
    accessibility/playstation/AXObjectCachePlayStation.cpp
    accessibility/playstation/AccessibilityObjectPlayStation.cpp

    # Generic pieces every non-Cocoa port lists itself.
    platform/generic/KeyedDecoderGeneric.cpp
    platform/generic/KeyedEncoderGeneric.cpp

    platform/text/Hyphenation.cpp
    platform/text/LocaleICU.cpp

    platform/unix/LoggingUnix.cpp
    platform/unix/SharedMemoryUnix.cpp   # needs patch WC5 (memfd_create)

    # --- New RitterOS files (each modeled on the file named after it) ---
    editing/ritteros/EditorRitterOS.cpp                       # editing/libwpe/EditorLibWPE.cpp (is #if USE(LIBWPE))

    page/ritteros/QuirkTableRitterOS.cpp                      # page/playstation/QuirkTablePlayStation.cpp

    platform/graphics/ritteros/GraphicsLayerRitterOS.cpp      # NEW: GraphicsLayer::create() returning a null layer
                                                              # (GraphicsLayerTextureMapper.cpp:35 is not compiled);
                                                              # draft in build/port-design/src/
    platform/graphics/ritteros/SystemFontDatabaseRitterOS.cpp # platform/graphics/playstation/SystemFontDatabasePlayStation.cpp

    platform/network/ritteros/CurlSSLHandleRitterOS.cpp       # platform/network/playstation/CurlSSLHandlePlayStation.cpp,
                                                              # but setCACertPath("/system/etc/ssl/cert.pem")
    platform/network/ritteros/NetworkStateNotifierRitterOS.cpp # platform/network/playstation/NetworkStateNotifierPlayStation.cpp

    platform/ritteros/MIMETypeRegistryRitterOS.cpp            # platform/playstation/MIMETypeRegistryPlayStation.cpp (no xdgmime)
    platform/ritteros/PasteboardRitterOS.cpp                  # platform/libwpe/PasteboardLibWPE.cpp, stubbed (M1: no clipboard)
    platform/ritteros/PlatformKeyboardEventRitterOS.cpp       # disambiguateKeyDownEvent(); platform/libwpe/PlatformKeyboardEventLibWPE.cpp:1330
    platform/ritteros/PlatformScreenRitterOS.cpp              # platform/playstation/PlatformScreenPlayStation.cpp
    platform/ritteros/UserAgentRitterOS.cpp                   # platform/playstation/UserAgentPlayStation.cpp
)

# The OpenSSL backend uses APIs deprecated in OpenSSL 3 (PlatformWPE.cmake:9-11, bug 245146).
list(APPEND WebCore_PRIVATE_DEFINITIONS OPENSSL_API_COMPAT=0x10100000L)

list(APPEND WebCore_LIBRARIES
    OpenSSL::Crypto
    # FontCacheSkia.cpp:69 calls FcConfigReference() itself and includes
    # <skia/ports/SkFontMgr_fontconfig.h> -> <fontconfig/fontconfig.h>; Skia links
    # these PRIVATE only (skia/CMakeLists.txt:836,853).
    Fontconfig::Fontconfig
    Freetype::Freetype
)

if (USE_WOFF2)
    # Static woff2dec needs woff2common and brotlidec behind it (as PlatformWin.cmake:238-245).
    list(APPEND WebCore_LIBRARIES
        Brotli::dec
        WOFF2::common
    )
endif ()

if (SHARPYUV_LIBS)
    list(APPEND WebCore_PRIVATE_LIBRARIES ${SHARPYUV_LIBS})   # as PlatformWin.cmake:247-249
endif ()

if (ENABLE_VIDEO)
    # The media engine (MediaPlayerPrivateRitterOS): FFmpeg's demuxers and
    # decoders, an LGPL build from deps/build.sh (static, in the sysroot), in
    # the order they need each other.
    list(APPEND WebCore_SOURCES
        platform/graphics/ritteros/AudioOutputRitterOS.cpp
        platform/graphics/ritteros/MediaPipelineRitterOS.cpp
        platform/graphics/ritteros/MediaPlayerPrivateRitterOS.cpp
    )
    if (ENABLE_MEDIA_SOURCE)
        list(APPEND WebCore_SOURCES
            platform/graphics/ritteros/MediaSourcePrivateRitterOS.cpp
            platform/graphics/ritteros/SourceBufferPrivateRitterOS.cpp
        )
    endif ()
    foreach (library avformat avcodec swscale swresample avutil)
        find_library(FFMPEG_${library}_LIBRARY ${library} REQUIRED)
        list(APPEND WebCore_PRIVATE_LIBRARIES ${FFMPEG_${library}_LIBRARY})
    endforeach ()
endif ()

if (ENABLE_DRAG_SUPPORT)
    list(APPEND WebCore_SOURCES
        platform/skia/DragImageSkia.cpp
    )
endif ()

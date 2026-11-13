# DRAFT: Source/WebCore/PAL/pal/PlatformRitterOS.cmake
#
# Identical to PAL/pal/PlatformPlayStation.cmake (no GLib, OpenSSL digests, the
# generic clock, the generic Sound stub). Not PlatformWPE.cmake: it needs
# system/glib/SleepDisablerGLib.cpp and GLib::GLib. Not PlatformWin.cmake:
# system/win/SoundWin.cpp. Not PlatformHaiku.cmake: system/haiku/SoundHaiku.cpp.

list(APPEND PAL_SOURCES
    crypto/openssl/CryptoDigestOpenSSL.cpp

    system/ClockGeneric.cpp
    system/Sound.cpp

    text/KillRing.cpp
)

list(APPEND PAL_LIBRARIES OpenSSL::Crypto)

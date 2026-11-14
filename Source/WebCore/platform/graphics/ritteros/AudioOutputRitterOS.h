/*
 * RitterOS: sound out for the media engine, in the sound device's format
 * (48 kHz, 16-bit, stereo, interleaved). It writes to /dev/pcm0 (one
 * program at a time has it) or, when that is taken, to the mixer soundd
 * (the local socket /run/sound); with neither the sound is dropped and the
 * pictures play on.
 *
 * RitterOS tells no program how much of what it wrote is still waiting to
 * be heard (OS request 12), so that is estimated: the device plays 48000
 * frames a second from when there is something to play, and holds at most
 * its ring (64 KiB: 16384 frames) beyond what a write is waiting to put in
 * (a write blocks while it is full). The player keeps that small (about
 * 0.1 s) by writing a sound only shortly before it is due.
 */

#pragma once

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include <wtf/MonotonicTime.h>
#include <wtf/Seconds.h>
#include <wtf/TZoneMalloc.h>

namespace WebCore {

class AudioOutputRitterOS {
    WTF_MAKE_TZONE_ALLOCATED(AudioOutputRitterOS);
public:
    static constexpr int sampleRate = 48000;
    static constexpr int channels = 2;

    AudioOutputRitterOS() = default;
    ~AudioOutputRitterOS();

    // Opens the device (or soundd) if it is not open. False when there is
    // no sound to be had (the frames written are then counted, not heard).
    bool open();
    // Waits for what was written to be heard, then lets the device go.
    void close();
    bool isOpen() const { return m_fd >= 0; }

    // Interleaved stereo 16-bit frames; blocks while the device is full.
    void write(const int16_t*, size_t frames);
    // The sound written and not yet heard (estimated).
    Seconds buffered() const;

private:
    int m_fd { -1 };
    bool m_failed { false };
    bool m_viaMixer { false };
    MonotonicTime m_emptyAt; // when the device will have played all it has
};

} // namespace WebCore

#endif

/*
 * RitterOS: sound out for the media engine, in the sound device's format
 * (48 kHz, 16-bit, stereo, interleaved). It plays through the mixer soundd
 * (the local socket /run/sound, an elf on every machine since OS step 68w),
 * so Compass plays beside other programs; without it, to /dev/pcm0 (one
 * program at a time has it); with neither the sound is dropped and the
 * pictures play on.
 *
 * Both say how much of what was written is still to be heard (OS request
 * 12, steps 68v/68w): soundd's "delay" message, the device's
 * SNDCTL_DSP_GETODELAY; and both drop it at once (soundd's "reset",
 * SNDCTL_DSP_RESET). An older system tells neither: then the delay is
 * estimated (48000 frames a second from the first write, at most the
 * device's ring beyond a write that blocked) and a close waits until
 * everything was heard.
 *
 * The last frames written are kept, so a pause can drop what was not heard
 * and the player write it again at the resume.
 */

#pragma once

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include <wtf/MonotonicTime.h>
#include <wtf/Seconds.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/Vector.h>

namespace WebCore {

class AudioOutputRitterOS {
    WTF_MAKE_TZONE_ALLOCATED(AudioOutputRitterOS);
public:
    static constexpr int sampleRate = 48000;
    static constexpr int channels = 2;

    AudioOutputRitterOS();
    ~AudioOutputRitterOS();

    // Opens soundd (or the device) if not open. False when there is no
    // sound to be had (the frames written are then counted, not heard).
    bool open();
    // Lets the device go: drops what was not heard where the system can,
    // else waits until it was.
    void close();
    bool isOpen() const { return m_fd >= 0; }
    // The delay is the system's (not estimated).
    bool exact() const { return m_fd >= 0 && m_exact; }

    // Interleaved stereo 16-bit frames; blocks while the device is full.
    void write(const int16_t*, size_t frames);
    // The sound written and not yet heard.
    Seconds buffered();
    // The last delay known, counted down since (asks nothing).
    Seconds bufferedEstimate() const { return std::max(0_s, m_emptyAt - MonotonicTime::now()); }
    // Drops what was not heard (where the system can) and gives it back.
    Vector<int16_t> takeUnheard();
    // Drops what was not heard (a seek).
    void reset();

private:
    bool openMixer();
    bool openDevice();
    size_t delayFrames();
    void keep(const int16_t*, size_t frames);

    int m_fd { -1 };
    bool m_failed { false };
    bool m_viaMixer { false };
    bool m_exact { false };
    MonotonicTime m_emptyAt; // (estimated) when everything written was heard
    // The last frames written, a ring of historyFrames.
    Vector<int16_t> m_history;
    size_t m_historyEnd { 0 }; // frames written in all (the ring's end)
};

} // namespace WebCore

#endif

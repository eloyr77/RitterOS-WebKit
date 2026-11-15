/*
 * Copyright (C) 2026 Compass, the web browser for RitterOS.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS BE LIABLE
 * FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

// RitterOS: Web Audio's output. A thread of its own renders the graph a
// chunk at a time, as the sound device takes it: it keeps about
// `queuedAhead` written and not yet heard, through the media engine's
// output (AudioOutputRitterOS: soundd, else /dev/pcm0). The device's rate
// is 48 kHz; a context at another rate is resampled (AudioDestinationResampler).
// Without sound (another user than the console's, no soundd), the output
// counts what it is given, so the graph still runs in real time and
// currentTime advances.

#pragma once

#if ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

#include "AudioDestinationResampler.h"
#include "AudioOutputRitterOS.h"
#include <wtf/Lock.h>
#include <wtf/Threading.h>

namespace WebCore {

class AudioDestinationRitterOS final : public AudioDestinationResampler, public ThreadSafeRefCounted<AudioDestinationRitterOS, WTF::DestructionThread::Main> {
public:
    explicit AudioDestinationRitterOS(const CreationOptions&);
    ~AudioDestinationRitterOS();

    void ref() const final { ThreadSafeRefCounted::ref(); }
    void deref() const final { ThreadSafeRefCounted::deref(); }

private:
    void startRendering(CompletionHandler<void(bool)>&&) final;
    void stopRendering(CompletionHandler<void(bool)>&&) final;
    MediaTime outputLatency() const final;

    void run();
    void stopThread();

    RefPtr<Thread> m_thread;
    std::atomic<bool> m_running { false };
    AudioOutputRitterOS m_output; // (the render thread's while it runs)
    std::atomic<double> m_latency { 0 }; // seconds written and not yet heard
};

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

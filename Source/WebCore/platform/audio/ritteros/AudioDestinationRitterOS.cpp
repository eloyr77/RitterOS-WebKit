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

#include "config.h"
#include "AudioDestinationRitterOS.h"

#if ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

#include "AudioBus.h"
#include "AudioUtilities.h"
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <wtf/MainThread.h>

namespace WebCore {

// A chunk: what the thread renders at a time (a multiple of the render
// quantum, 128 frames), about 10.7 ms at 48 kHz.
static constexpr size_t chunkFrames = 4 * AudioUtilities::renderQuantumSize;
// What is kept written and not yet heard: enough that a busy moment does
// not starve the device, little enough that the page's changes are heard
// soon.
static constexpr Seconds queuedAhead = 60_ms;

Ref<AudioDestination> AudioDestination::create(const CreationOptions& options)
{
    if (options.numberOfInputChannels)
        RELEASE_LOG(Media, "AudioDestination::create(%u, %u, %f) - no audio input on RitterOS", options.numberOfInputChannels, options.numberOfOutputChannels, options.sampleRate);
    return adoptRef(*new AudioDestinationRitterOS(options));
}

float AudioDestination::hardwareSampleRate()
{
    return AudioOutputRitterOS::sampleRate;
}

unsigned long AudioDestination::maxChannelCount()
{
    return AudioOutputRitterOS::channels;
}

AudioDestinationRitterOS::AudioDestinationRitterOS(const CreationOptions& options)
    : AudioDestinationResampler(options, hardwareSampleRate())
{
}

AudioDestinationRitterOS::~AudioDestinationRitterOS()
{
    stopThread();
}

void AudioDestinationRitterOS::startRendering(CompletionHandler<void(bool)>&& completionHandler)
{
    ASSERT(isMainThread());
    if (!m_thread) {
        m_running = true;
        m_thread = Thread::create("Web Audio"_s, [this] {
            run();
        }, ThreadType::Audio, Thread::QOS::UserInteractive);
    }
    setIsPlaying(true);
    callOnMainThread([completionHandler = WTF::move(completionHandler)]() mutable {
        completionHandler(true);
    });
}

void AudioDestinationRitterOS::stopRendering(CompletionHandler<void(bool)>&& completionHandler)
{
    ASSERT(isMainThread());
    stopThread();
    setIsPlaying(false);
    callOnMainThread([completionHandler = WTF::move(completionHandler)]() mutable {
        completionHandler(true);
    });
}

void AudioDestinationRitterOS::stopThread()
{
    if (!m_thread)
        return;
    m_running = false;
    m_thread->waitForCompletion();
    m_thread = nullptr;
}

MediaTime AudioDestinationRitterOS::outputLatency() const
{
    return MediaTime::createWithDouble(m_latency.load());
}

void AudioDestinationRitterOS::run()
{
    unsigned outputChannels = m_outputBus->numberOfChannels();
    Vector<float> left(chunkFrames);
    Vector<float> right(chunkFrames);
    Vector<int16_t> frames(chunkFrames * AudioOutputRitterOS::channels);
    uint64_t framesWritten = 0;

    m_output.open(); // (without sound, it counts what it is given)
    while (m_running) {
        Seconds ahead = m_output.buffered();
        m_latency = ahead.seconds();
        if (ahead > queuedAhead) {
            auto wait = std::min(ahead - queuedAhead, 10_ms);
            usleep(static_cast<useconds_t>(wait.microseconds()));
            continue;
        }

        m_outputBus->setChannelMemory(0, left.mutableSpan());
        if (outputChannels > 1)
            m_outputBus->setChannelMemory(1, right.mutableSpan());
        size_t framesToRender = pullRendered(chunkFrames);
        double sampleTime = static_cast<double>(framesWritten) / AudioOutputRitterOS::sampleRate;
        render(sampleTime, MonotonicTime::now() + ahead, framesToRender);

        // To the device's format: 16-bit stereo (mono is heard on both sides).
        auto& second = outputChannels > 1 ? right : left;
        for (size_t i = 0; i < chunkFrames; i++) {
            auto sample = [](float value) {
                return static_cast<int16_t>(std::lrint(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
            };
            frames[i * 2] = sample(left[i]);
            frames[i * 2 + 1] = sample(second[i]);
        }
        m_output.write(frames.span().data(), chunkFrames);
        framesWritten += chunkFrames;
    }
    m_output.close();
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

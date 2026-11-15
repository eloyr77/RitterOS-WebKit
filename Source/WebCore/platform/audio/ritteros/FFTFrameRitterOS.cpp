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

// RitterOS: Web Audio's FFT on FFmpeg's real FFT (libavutil/tx.h), the
// library the media engine carries already.
//
// The frame keeps what the Mac's (cocoa/FFTFrameCocoa.cpp) keeps, which the
// shared code (FFTFrame.cpp, PeriodicWave, RealtimeAnalyser, FFTConvolver)
// expects: bins 0 to fftSize/2 - 1 in realData/imagData, at the textbook
// scale (X[k] = sum of x[n] e^(-2 pi i k n / N)), with the Nyquist bin's real
// part in imagData[0] (DC and Nyquist have no imaginary part); the inverse
// gives x back (scaled by 1/N).

#include "config.h"
#include "FFTFrame.h"

#if ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

#include <bit>

extern "C" {
#include <libavutil/tx.h>
}

namespace WebCore {

static constexpr unsigned minimumFFTPow2Size = 2;
static constexpr unsigned maximumFFTPow2Size = 24;

FFTFrame::FFTFrame(unsigned fftSize)
    : m_FFTSize(fftSize)
    , m_log2FFTSize(std::bit_width(fftSize) - 1)
    , m_realData(fftSize)
    , m_imagData(fftSize)
{
    ASSERT(1U << m_log2FFTSize == m_FFTSize);
    createTransforms();
}

// A blank frame (createInterpolatedFrame() gives it its size).
FFTFrame::FFTFrame()
    : m_FFTSize(0)
    , m_log2FFTSize(0)
{
}

FFTFrame::FFTFrame(const FFTFrame& frame)
    : m_FFTSize(frame.m_FFTSize)
    , m_log2FFTSize(frame.m_log2FFTSize)
    , m_realData(frame.m_FFTSize)
    , m_imagData(frame.m_FFTSize)
{
    createTransforms();
    memcpySpan(m_realData.span(), frame.m_realData.span());
    memcpySpan(m_imagData.span(), frame.m_imagData.span());
}

FFTFrame::~FFTFrame()
{
    destroyTransforms();
}

void FFTFrame::createTransforms()
{
    if (m_FFTSize < 2)
        return;
    m_samples.resize(m_FFTSize);
    m_complex.resize((m_FFTSize / 2 + 1) * 2);
    float forwardScale = 1;
    float inverseScale = 1.0f / m_FFTSize;
    av_tx_fn forward = nullptr;
    av_tx_fn inverse = nullptr;
    if (av_tx_init(&m_forward, &forward, AV_TX_FLOAT_RDFT, 0, m_FFTSize, &forwardScale, AV_TX_UNALIGNED) < 0
        || av_tx_init(&m_inverse, &inverse, AV_TX_FLOAT_RDFT, 1, m_FFTSize, &inverseScale, AV_TX_UNALIGNED) < 0) {
        RELEASE_LOG_ERROR(WebAudio, "FFTFrame: no FFmpeg real FFT of size %u", m_FFTSize);
        destroyTransforms();
        return;
    }
    m_forwardFunction = forward;
    m_inverseFunction = inverse;
}

void FFTFrame::destroyTransforms()
{
    av_tx_uninit(&m_forward);
    av_tx_uninit(&m_inverse);
    m_forwardFunction = nullptr;
    m_inverseFunction = nullptr;
}

void FFTFrame::initialize()
{
}

void FFTFrame::doFFT(std::span<const float> data)
{
    unsigned halfSize = m_FFTSize / 2;
    auto realData = m_realData.span();
    auto imagData = m_imagData.span();
    if (!m_forwardFunction) {
        zeroSpan(realData);
        zeroSpan(imagData);
        return;
    }
    memcpySpan(m_samples.mutableSpan(), data.first(m_FFTSize));
    m_forwardFunction(m_forward, m_complex.mutableSpan().data(), m_samples.mutableSpan().data(), sizeof(float));

    auto complex = m_complex.span();
    realData[0] = complex[0]; // DC
    imagData[0] = complex[halfSize * 2]; // Nyquist (real)
    for (unsigned k = 1; k < halfSize; k++) {
        realData[k] = complex[k * 2];
        imagData[k] = complex[k * 2 + 1];
    }
}

void FFTFrame::doInverseFFT(std::span<float> data)
{
    unsigned halfSize = m_FFTSize / 2;
    if (!m_inverseFunction) {
        zeroSpan(data.first(m_FFTSize));
        return;
    }
    auto realData = m_realData.span();
    auto imagData = m_imagData.span();
    auto complex = m_complex.mutableSpan();
    complex[0] = realData[0];
    complex[1] = 0;
    complex[halfSize * 2] = imagData[0];
    complex[halfSize * 2 + 1] = 0;
    for (unsigned k = 1; k < halfSize; k++) {
        complex[k * 2] = realData[k];
        complex[k * 2 + 1] = imagData[k];
    }
    // (The inverse overwrites its input, m_complex.)
    m_inverseFunction(m_inverse, m_samples.mutableSpan().data(), complex.data(), 2 * sizeof(float));
    memcpySpan(data.first(m_FFTSize), m_samples.span());
}

int FFTFrame::minFFTSize()
{
    return 1 << minimumFFTPow2Size;
}

int FFTFrame::maxFFTSize()
{
    return 1 << maximumFFTPow2Size;
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

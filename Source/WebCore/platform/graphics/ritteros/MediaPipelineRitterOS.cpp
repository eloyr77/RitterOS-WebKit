/*
 * RitterOS: the playing half of the media engine (MediaPipelineRitterOS.h).
 */

#include "config.h"
#include "MediaPipelineRitterOS.h"

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include "AudioOutputRitterOS.h"
#include <wtf/MainThread.h>
#include <wtf/text/WTFString.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkImage.h>
#include <skia/core/SkPixmap.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

namespace WebCore {

// Pictures decoded ahead (720p BGRA: 3.7 MB each), packets queued ahead.
static constexpr size_t maxPictures = 6;
static constexpr size_t maxVideoPacketBytes = 8 * 1024 * 1024;
static constexpr size_t maxVideoPackets = 240;
static constexpr size_t maxAudioPacketBytes = 1024 * 1024;
static constexpr size_t maxAudioPackets = 400;
// The sound is written this far ahead of its time (what the device holds).
static constexpr Seconds audioLead = 120_ms;
// Late by this much with nothing to show: hold the clock (stalled).
static constexpr double stallSlack = 0.1;

static int64_t toMicroseconds(const MediaTime& time)
{
    return time.isValid() ? llround(time.toDouble() * 1000000) : AV_NOPTS_VALUE;
}

static MediaTime fromMicroseconds(int64_t value)
{
    return value == AV_NOPTS_VALUE ? MediaTime::invalidTime() : MediaTime(value, 1000000);
}

// --- MediaCodecConfigRitterOS ------------------------------------------------

Ref<MediaCodecConfigRitterOS> MediaCodecConfigRitterOS::create(const AVCodecParameters* parameters, int, int)
{
    Ref config = adoptRef(*new MediaCodecConfigRitterOS);
    config->m_parameters = avcodec_parameters_alloc();
    avcodec_parameters_copy(config->m_parameters, parameters);
    return config;
}

MediaCodecConfigRitterOS::~MediaCodecConfigRitterOS()
{
    avcodec_parameters_free(&m_parameters);
}

bool MediaCodecConfigRitterOS::isVideo() const { return m_parameters->codec_type == AVMEDIA_TYPE_VIDEO; }
bool MediaCodecConfigRitterOS::isAudio() const { return m_parameters->codec_type == AVMEDIA_TYPE_AUDIO; }

int MediaCodecConfigRitterOS::width() const
{
    auto ratio = m_parameters->sample_aspect_ratio;
    if (ratio.num > 0 && ratio.den > 0 && ratio.num != ratio.den)
        return static_cast<int>(llround(static_cast<double>(m_parameters->width) * ratio.num / ratio.den));
    return m_parameters->width;
}

int MediaCodecConfigRitterOS::height() const { return m_parameters->height; }

String MediaCodecConfigRitterOS::codecName() const
{
    return String::fromLatin1(avcodec_get_name(m_parameters->codec_id));
}

bool MediaCodecConfigRitterOS::sameDecoderAs(const MediaCodecConfigRitterOS& other) const
{
    if (this == &other)
        return true;
    auto* a = m_parameters;
    auto* b = other.m_parameters;
    if (a->codec_id != b->codec_id || a->extradata_size != b->extradata_size)
        return false;
    if (a->extradata_size && memcmp(a->extradata, b->extradata, a->extradata_size))
        return false;
    if (a->codec_type == AVMEDIA_TYPE_AUDIO)
        return a->sample_rate == b->sample_rate && a->ch_layout.nb_channels == b->ch_layout.nb_channels;
    return true;
}

// --- MediaPipelineRitterOS ------------------------------------------------------

Ref<MediaPipelineRitterOS> MediaPipelineRitterOS::create()
{
    // (FFmpeg's notes would fill Compass's log: only what stops it.)
    av_log_set_level(AV_LOG_FATAL);
    Ref pipeline = adoptRef(*new MediaPipelineRitterOS);
    pipeline->m_videoThread = Thread::create("Media video"_s, [pipeline = pipeline.copyRef()] {
        pipeline->videoThreadMain();
    });
    pipeline->m_audioThread = Thread::create("Media audio"_s, [pipeline = pipeline.copyRef()] {
        pipeline->audioThreadMain();
    });
    return pipeline;
}

MediaPipelineRitterOS::~MediaPipelineRitterOS() = default;

void MediaPipelineRitterOS::stop()
{
    {
        Locker locker { m_lock };
        m_stopping = true;
        m_condition.notifyAll();
    }
    if (m_videoThread)
        m_videoThread->detach();
    if (m_audioThread)
        m_audioThread->detach();
    m_videoThread = nullptr;
    m_audioThread = nullptr;
}

void MediaPipelineRitterOS::setWakeMainThread(Function<void()>&& wake)
{
    Locker locker { m_wakeLock };
    m_wakeMainThread = WTF::move(wake);
}

void MediaPipelineRitterOS::wakeMainThread()
{
    Locker locker { m_wakeLock };
    if (m_wakeMainThread)
        m_wakeMainThread();
}

void MediaPipelineRitterOS::setHasVideo(bool hasVideo)
{
    Locker locker { m_lock };
    m_hasVideo = hasVideo;
}

void MediaPipelineRitterOS::setHasAudio(bool hasAudio)
{
    Locker locker { m_lock };
    m_hasAudio = hasAudio;
}

bool MediaPipelineRitterOS::canAcceptVideo() const
{
    Locker locker { m_lock };
    return m_videoPacketBytes < maxVideoPacketBytes && m_videoPackets.size() < maxVideoPackets;
}

bool MediaPipelineRitterOS::canAcceptAudio() const
{
    Locker locker { m_lock };
    return m_audioPacketBytes < maxAudioPacketBytes && m_audioPackets.size() < maxAudioPackets;
}

bool MediaPipelineRitterOS::pushVideo(MediaPacketRitterOS&& packet)
{
    Locker locker { m_lock };
    if (m_videoPacketBytes >= maxVideoPacketBytes || m_videoPackets.size() >= maxVideoPackets)
        return false;
    m_videoPacketBytes += packet.data.size();
    m_videoPackets.append(WTF::move(packet));
    m_videoInputEnded = false;
    m_videoDecodingEnded = false;
    m_condition.notifyAll();
    return true;
}

bool MediaPipelineRitterOS::pushAudio(MediaPacketRitterOS&& packet)
{
    Locker locker { m_lock };
    if (m_audioPacketBytes >= maxAudioPacketBytes || m_audioPackets.size() >= maxAudioPackets)
        return false;
    m_audioPacketBytes += packet.data.size();
    m_audioPackets.append(WTF::move(packet));
    m_audioInputEnded = false;
    m_audioPlayingEnded = false;
    m_condition.notifyAll();
    return true;
}

void MediaPipelineRitterOS::endOfVideo()
{
    Locker locker { m_lock };
    m_videoInputEnded = true;
    m_condition.notifyAll();
}

void MediaPipelineRitterOS::endOfAudio()
{
    Locker locker { m_lock };
    m_audioInputEnded = true;
    m_condition.notifyAll();
}

void MediaPipelineRitterOS::flush(const MediaTime& time)
{
    Locker locker { m_lock };
    m_videoGeneration++;
    m_audioGeneration++;
    m_videoPackets.clear();
    m_audioPackets.clear();
    m_videoPacketBytes = m_audioPacketBytes = 0;
    m_pictures.clear();
    m_videoInputEnded = m_audioInputEnded = false;
    m_videoDecodingEnded = m_audioPlayingEnded = false;
    m_flushTime = time;
    m_clockBase = time;
    m_clockStart = MonotonicTime::now();
    m_current.time = MediaTime::invalidTime(); // (the next picture replaces it)
    m_lastQueuedVideoTime = MediaTime::invalidTime();
    m_audioStarved = true;
    m_condition.notifyAll();
}

void MediaPipelineRitterOS::flushVideo()
{
    // (MSE: a track's samples come again from the seek's time; the clock
    // was set by flush().)
    Locker locker { m_lock };
    m_videoGeneration++;
    m_videoPackets.clear();
    m_videoPacketBytes = 0;
    m_pictures.clear();
    m_videoInputEnded = m_videoDecodingEnded = false;
    m_condition.notifyAll();
}

void MediaPipelineRitterOS::flushAudio()
{
    Locker locker { m_lock };
    m_audioGeneration++;
    m_audioPackets.clear();
    m_audioPacketBytes = 0;
    m_audioInputEnded = m_audioPlayingEnded = false;
    m_condition.notifyAll();
}

MediaTime MediaPipelineRitterOS::clockLocked() const
{
    if (!m_playing || m_stalled)
        return m_clockBase;
    return m_clockBase + MediaTime::createWithDouble((MonotonicTime::now() - m_clockStart).seconds());
}

void MediaPipelineRitterOS::holdClockLocked()
{
    m_clockBase = clockLocked();
    m_clockStart = MonotonicTime::now();
}

void MediaPipelineRitterOS::updateStallLocked()
{
    if (!m_playing) {
        m_stalled = false;
        return;
    }
    bool stalled = false;
    auto clock = clockLocked();
    if (m_hasVideo && !m_videoDecodingEnded && m_pictures.isEmpty()) {
        // Nothing decoded: hold at the first picture, or once late.
        if (!m_current.image || !m_current.time.isValid() || (clock - m_current.time).toDouble() > stallSlack)
            stalled = true;
    }
    if (m_hasAudio && m_audioStarved && !m_audioInputEnded && !m_audioPlayingEnded)
        stalled = true;
    if (stalled == m_stalled)
        return;
    if (stalled)
        holdClockLocked();
    m_stalled = stalled;
    if (!stalled)
        m_clockStart = MonotonicTime::now();
    m_condition.notifyAll();
}

void MediaPipelineRitterOS::setPlaying(bool playing)
{
    Locker locker { m_lock };
    if (m_playing == playing)
        return;
    holdClockLocked();
    m_playing = playing;
    m_stalled = false;
    updateStallLocked();
    m_condition.notifyAll();
}

bool MediaPipelineRitterOS::isPlaying() const
{
    Locker locker { m_lock };
    return m_playing;
}

void MediaPipelineRitterOS::setVolume(float volume)
{
    Locker locker { m_lock };
    m_volume = volume;
}

void MediaPipelineRitterOS::setMuted(bool muted)
{
    Locker locker { m_lock };
    m_muted = muted;
}

MediaTime MediaPipelineRitterOS::currentTime() const
{
    Locker locker { m_lock };
    return clockLocked();
}

bool MediaPipelineRitterOS::isStalled() const
{
    Locker locker { m_lock };
    return m_stalled;
}

bool MediaPipelineRitterOS::isEnded() const
{
    Locker locker { m_lock };
    if (!m_hasVideo && !m_hasAudio)
        return false;
    bool videoDone = !m_hasVideo || (m_videoDecodingEnded && m_pictures.isEmpty());
    bool audioDone = !m_hasAudio || m_audioPlayingEnded;
    return videoDone && audioDone;
}

bool MediaPipelineRitterOS::updateCurrentImage()
{
    Locker locker { m_lock };
    updateStallLocked();
    auto clock = clockLocked();
    bool changed = false;
    // After a flush the first picture comes at once (a seek's picture).
    if (!m_pictures.isEmpty() && (!m_current.image || !m_current.time.isValid())) {
        m_current = m_pictures.takeFirst();
        changed = true;
    }
    while (!m_pictures.isEmpty() && m_pictures.first().time <= clock) {
        if (changed) {
            m_droppedFrames++;
            m_statsSkipped++;
        }
        m_current = m_pictures.takeFirst();
        changed = true;
    }
    if (changed) {
        if (m_playing && !m_stalled && m_current.time.isValid()) {
            double late = std::max(0.0, (clock - m_current.time).toDouble());
            m_statsShown++;
            m_statsLateSum += late;
            m_statsLateMax = std::max(m_statsLateMax, late);
        }
        m_hasFirstImage = true;
        updateStallLocked();
        m_condition.notifyAll();
    }
    return changed;
}

std::optional<Seconds> MediaPipelineRitterOS::timeUntilNextPicture() const
{
    Locker locker { m_lock };
    if (!m_playing || m_stalled || m_pictures.isEmpty())
        return std::nullopt;
    return Seconds((m_pictures.first().time - clockLocked()).toDouble());
}

void MediaPipelineRitterOS::reportStatistics()
{
    Locker locker { m_lock };
    dprintf(2, "media: %u pictures shown, %u skipped, late %.1f ms on average, %.1f ms at most\n", m_statsShown, m_statsSkipped,
        m_statsShown ? m_statsLateSum / m_statsShown * 1000 : 0.0, m_statsLateMax * 1000);
    m_statsShown = m_statsSkipped = 0;
    m_statsLateSum = m_statsLateMax = 0;
}

RefPtr<NativeImage> MediaPipelineRitterOS::currentImage() const
{
    Locker locker { m_lock };
    return m_current.image;
}

bool MediaPipelineRitterOS::hasFirstImage() const
{
    Locker locker { m_lock };
    return m_hasFirstImage || !m_pictures.isEmpty();
}

MediaTime MediaPipelineRitterOS::lastQueuedVideoTime() const
{
    Locker locker { m_lock };
    return m_lastQueuedVideoTime;
}

unsigned MediaPipelineRitterOS::decodedFrameCount() const
{
    Locker locker { m_lock };
    return m_decodedFrames;
}

unsigned MediaPipelineRitterOS::droppedFrameCount() const
{
    Locker locker { m_lock };
    return m_droppedFrames;
}

static AVCodecContext* openDecoder(const MediaCodecConfigRitterOS& config)
{
    auto* codec = avcodec_find_decoder(config.parameters()->codec_id);
    if (!codec)
        return nullptr;
    auto* context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(context, config.parameters());
    context->pkt_timebase = { 1, 1000000 };
    if (config.isVideo()) {
        context->thread_count = 0; // (a thread per core)
        context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    if (avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        return nullptr;
    }
    return context;
}

static void fillPacket(AVPacket* packet, const MediaPacketRitterOS& source)
{
    av_packet_unref(packet);
    if (av_new_packet(packet, source.data.size()) < 0)
        return;
    memcpy(packet->data, source.data.span().data(), source.data.size());
    packet->pts = toMicroseconds(source.presentationTime);
    packet->dts = toMicroseconds(source.decodeTime);
    packet->duration = source.duration.isValid() ? toMicroseconds(source.duration) : 0;
    if (source.isKeyFrame)
        packet->flags |= AV_PKT_FLAG_KEY;
    if (source.isNonDisplaying)
        packet->flags |= AV_PKT_FLAG_DISCARD;
}

static RefPtr<NativeImage> convertPicture(AVFrame* frame, SwsContext*& scaler)
{
    int width = frame->width, height = frame->height;
    if (width <= 0 || height <= 0)
        return nullptr;
    scaler = sws_getCachedContext(scaler, width, height, static_cast<AVPixelFormat>(frame->format), width, height,
        AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!scaler)
        return nullptr;
    size_t stride = static_cast<size_t>(width) * 4;
    auto* pixels = static_cast<uint8_t*>(fastMalloc(stride * height));
    uint8_t* planes[4] = { pixels, nullptr, nullptr, nullptr };
    int strides[4] = { static_cast<int>(stride), 0, 0, 0 };
    sws_scale(scaler, frame->data, frame->linesize, 0, height, planes, strides);
    auto info = SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kOpaque_SkAlphaType);
    SkPixmap pixmap(info, pixels, stride);
    auto image = SkImages::RasterFromPixmap(pixmap, [](const void* pixels, void*) {
        fastFree(const_cast<void*>(pixels));
    }, nullptr);
    if (!image) {
        fastFree(pixels);
        return nullptr;
    }
    return NativeImage::create(WTF::move(image));
}

void MediaPipelineRitterOS::videoThreadMain()
{
    AVCodecContext* decoder = nullptr;
    RefPtr<MediaCodecConfigRitterOS> config;
    SwsContext* scaler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    uint64_t generation = 0;
    bool draining = false;

    while (true) {
        std::optional<MediaPacketRitterOS> input;
        MediaTime flushTime;
        {
            Locker locker { m_lock };
            while (!m_stopping && generation == m_videoGeneration && m_videoPackets.isEmpty()
                && !(m_videoInputEnded && !m_videoDecodingEnded))
                m_condition.wait(m_lock);
            if (m_stopping)
                break;
            if (generation != m_videoGeneration) {
                generation = m_videoGeneration;
                if (decoder)
                    avcodec_flush_buffers(decoder);
                draining = false;
            }
            if (!m_videoPackets.isEmpty()) {
                input = m_videoPackets.takeFirst();
                m_videoPacketBytes -= input->data.size();
            } else if (!m_videoInputEnded || m_videoDecodingEnded)
                continue;
            flushTime = m_flushTime;
            m_condition.notifyAll();
        }
        wakeMainThread(); // (room for more packets)

        if (input) {
            if (!config || !input->config->sameDecoderAs(*config)) {
                if (decoder)
                    avcodec_free_context(&decoder);
                config = input->config;
                decoder = openDecoder(*config);
                if (!decoder)
                    dprintf(2, "media: no decoder for %s\n", avcodec_get_name(config->parameters()->codec_id));
            }
            if (!decoder)
                continue;
            fillPacket(packet, *input);
            if (avcodec_send_packet(decoder, packet) < 0)
                continue;
        } else {
            if (!decoder || draining) {
                Locker locker { m_lock };
                m_videoDecodingEnded = true;
                m_condition.notifyAll();
                continue;
            }
            avcodec_send_packet(decoder, nullptr); // drain what it holds
            draining = true;
        }

        while (avcodec_receive_frame(decoder, frame) >= 0) {
            auto time = fromMicroseconds(frame->best_effort_timestamp);
            auto frameEnd = time + fromMicroseconds(frame->duration > 0 ? frame->duration : 0);
            bool beforeSeek = flushTime.isValid() && time.isValid() && frameEnd < flushTime;
            RefPtr<NativeImage> image = beforeSeek ? nullptr : convertPicture(frame, scaler);
            av_frame_unref(frame);
            Locker locker { m_lock };
            if (generation != m_videoGeneration || m_stopping)
                break;
            m_decodedFrames++;
            if (!image)
                continue;
            bool wasEmpty = m_pictures.isEmpty();
            m_pictures.append({ time, WTF::move(image) });
            m_lastQueuedVideoTime = time;
            if (wasEmpty)
                wakeMainThread(); // (posts to the main thread; takes no lock here)
            while (!m_stopping && generation == m_videoGeneration && m_pictures.size() >= maxPictures)
                m_condition.wait(m_lock);
        }
        if (draining) {
            Locker locker { m_lock };
            if (generation == m_videoGeneration) {
                m_videoDecodingEnded = true;
                m_condition.notifyAll();
            }
            locker.unlockEarly();
            avcodec_flush_buffers(decoder);
            draining = false;
            wakeMainThread();
        }
    }
    if (decoder)
        avcodec_free_context(&decoder);
    sws_freeContext(scaler);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

void MediaPipelineRitterOS::audioThreadMain()
{
    AVCodecContext* decoder = nullptr;
    RefPtr<MediaCodecConfigRitterOS> config;
    SwrContext* resampler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AudioOutputRitterOS output;
    Vector<int16_t> pcm;
    uint64_t generation = 0;

    // Writes frames (48 kHz stereo) starting at time when they are due.
    // False when a flush or the end came first.
    auto play = [&](int16_t* samples, size_t count, MediaTime start) -> bool {
        while (count) {
            float gain;
            {
                Locker locker { m_lock };
                if (m_stopping || generation != m_audioGeneration)
                    return false;
                if (!m_playing) {
                    locker.unlockEarly();
                    output.close(); // (lets the device go while paused)
                    Locker waitLocker { m_lock };
                    while (!m_stopping && generation == m_audioGeneration && !m_playing)
                        m_condition.wait(m_lock);
                    continue;
                }
                auto clock = clockLocked();
                if (m_audioStarved) {
                    m_audioStarved = false;
                    updateStallLocked();
                    clock = clockLocked();
                }
                output.open();
                double lead = (start - clock).toDouble() - output.buffered().seconds();
                if (lead > audioLead.seconds()) {
                    // Early: wait (the clock may be held for the pictures).
                    m_condition.waitFor(m_lock, Seconds(std::min(lead - audioLead.seconds() + 0.005, 0.05)));
                    continue;
                }
                if (lead < -0.08) {
                    // Late (a seek, a stall): drop what is past.
                    size_t skip = std::min<size_t>(count, static_cast<size_t>(-lead * AudioOutputRitterOS::sampleRate));
                    samples += skip * 2;
                    count -= skip;
                    start = start + MediaTime(static_cast<int64_t>(skip), AudioOutputRitterOS::sampleRate);
                    continue;
                }
                gain = m_muted ? 0 : m_volume;
            }
            size_t chunk = std::min<size_t>(count, 1024);
            if (gain < 0.999f) {
                for (size_t i = 0; i < chunk * 2; i++)
                    samples[i] = static_cast<int16_t>(samples[i] * gain);
            }
            output.write(samples, chunk);
            samples += chunk * 2;
            count -= chunk;
            start = start + MediaTime(static_cast<int64_t>(chunk), AudioOutputRitterOS::sampleRate);
        }
        return true;
    };

    while (true) {
        std::optional<MediaPacketRitterOS> input;
        bool ended = false;
        {
            Locker locker { m_lock };
            while (!m_stopping && generation == m_audioGeneration && m_audioPackets.isEmpty()
                && !(m_audioInputEnded && !m_audioPlayingEnded)) {
                if (m_playing && !m_audioStarved && output.buffered() < 20_ms) {
                    m_audioStarved = true; // nothing to play: hold the clock
                    updateStallLocked();
                }
                m_condition.waitFor(m_lock, 20_ms);
            }
            if (m_stopping)
                break;
            if (generation != m_audioGeneration) {
                generation = m_audioGeneration;
                if (decoder)
                    avcodec_flush_buffers(decoder);
                if (resampler)
                    swr_free(&resampler);
            }
            if (!m_audioPackets.isEmpty()) {
                input = m_audioPackets.takeFirst();
                m_audioPacketBytes -= input->data.size();
            } else if (m_audioInputEnded && !m_audioPlayingEnded)
                ended = true;
            else
                continue;
            m_condition.notifyAll();
        }
        wakeMainThread();

        if (ended) {
            if (decoder)
                avcodec_send_packet(decoder, nullptr);
        } else {
            if (!config || !input->config->sameDecoderAs(*config)) {
                if (decoder)
                    avcodec_free_context(&decoder);
                if (resampler)
                    swr_free(&resampler);
                config = input->config;
                decoder = openDecoder(*config);
                if (!decoder)
                    dprintf(2, "media: no decoder for %s\n", avcodec_get_name(config->parameters()->codec_id));
            }
            if (!decoder)
                continue;
            fillPacket(packet, *input);
            if (avcodec_send_packet(decoder, packet) < 0)
                continue;
        }

        bool flushed = false;
        while (decoder && avcodec_receive_frame(decoder, frame) >= 0) {
            auto start = fromMicroseconds(frame->best_effort_timestamp);
            if (!resampler) {
                AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, AudioOutputRitterOS::sampleRate,
                    &frame->ch_layout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr);
                if (!resampler || swr_init(resampler) < 0) {
                    av_frame_unref(frame);
                    continue;
                }
            }
            int room = static_cast<int>(av_rescale_rnd(swr_get_delay(resampler, frame->sample_rate) + frame->nb_samples,
                AudioOutputRitterOS::sampleRate, frame->sample_rate, AV_ROUND_UP));
            if (pcm.size() < static_cast<size_t>(room) * 2)
                pcm.grow(static_cast<size_t>(room) * 2);
            uint8_t* out[1] = { reinterpret_cast<uint8_t*>(pcm.mutableSpan().data()) };
            int count = swr_convert(resampler, out, room, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            av_frame_unref(frame);
            if (count <= 0)
                continue;
            if (!start.isValid())
                continue;
            if (!play(pcm.mutableSpan().data(), count, start)) {
                flushed = true;
                break;
            }
        }
        if (ended && !flushed) {
            // Everything written: wait until it was heard, then say so.
            while (output.buffered() > 0_s) {
                Locker locker { m_lock };
                if (m_stopping || generation != m_audioGeneration)
                    break;
                m_condition.waitFor(m_lock, std::min(output.buffered(), 50_ms));
            }
            Locker locker { m_lock };
            if (generation == m_audioGeneration)
                m_audioPlayingEnded = true;
            locker.unlockEarly();
            if (decoder)
                avcodec_flush_buffers(decoder);
            wakeMainThread();
        }
    }
    output.close();
    if (decoder)
        avcodec_free_context(&decoder);
    swr_free(&resampler);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

} // namespace WebCore

#endif

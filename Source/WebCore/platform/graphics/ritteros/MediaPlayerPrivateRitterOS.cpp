/*
 * RitterOS: WebKit's media engine (MediaPlayerPrivateRitterOS.h).
 */

#include "config.h"
#include "MediaPlayerPrivateRitterOS.h"

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include "ContentType.h"
#include "GraphicsContext.h"
#include "HTTPHeaderNames.h"
#include "MediaPipelineRitterOS.h"
#include "MediaPlayer.h"
#include "NativeImage.h"
#include "PlatformMediaResourceLoader.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SharedBuffer.h"
#include "VideoPlaybackQualityMetrics.h"
#include <wtf/Condition.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/Threading.h>
#include <wtf/text/StringToIntegerConversion.h>

#if ENABLE(MEDIA_SOURCE)
#include "MediaSourcePrivateClient.h"
#include "MediaSourcePrivateRitterOS.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace WebCore {

// --- The engine's registration and the types it plays --------------------------

class MediaPlayerFactoryRitterOS final : public MediaPlayerFactory {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(MediaPlayerFactoryRitterOS);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(MediaPlayerFactoryRitterOS);
private:
    MediaPlayerEnums::MediaEngineIdentifier identifier() const final { return MediaPlayerEnums::MediaEngineIdentifier::RitterOS; }
    Ref<MediaPlayerPrivateInterface> createMediaEnginePlayer(MediaPlayer& player) const final { return adoptRef(*new MediaPlayerPrivateRitterOS(player)); }
    void getSupportedTypes(HashSet<String>& types) const final { MediaPlayerPrivateRitterOS::getSupportedTypes(types); }
    MediaPlayer::SupportsType supportsTypeAndCodecs(const MediaEngineSupportParameters& parameters) const final { return MediaPlayerPrivateRitterOS::supportsType(parameters); }
};

void MediaPlayerPrivateRitterOS::registerMediaEngine(MediaEngineRegistrar registrar)
{
    registrar(makeUnique<MediaPlayerFactoryRitterOS>());
}

static const HashSet<String>& fileTypes()
{
    static NeverDestroyed types = HashSet<String> {
        "video/mp4"_s, "audio/mp4"_s, "video/x-m4v"_s, "audio/x-m4a"_s, "video/quicktime"_s,
        "video/webm"_s, "audio/webm"_s,
        "audio/mpeg"_s, "audio/mp3"_s, "audio/x-mp3"_s, "audio/mpeg3"_s, "audio/x-mpeg"_s,
        "audio/ogg"_s, "video/ogg"_s, "application/ogg"_s, "audio/opus"_s,
        "audio/wav"_s, "audio/wave"_s, "audio/x-wav"_s, "audio/vnd.wave"_s,
        "audio/flac"_s, "audio/x-flac"_s, "audio/aac"_s, "audio/x-aac"_s,
    };
    return types;
}

#if ENABLE(MEDIA_SOURCE)
// What MediaSource.isTypeSupported says yes to: fragmented MP4 and WebM
// (the containers SourceBufferPrivateRitterOS cuts into segments).
static const HashSet<String>& mediaSourceTypes()
{
    static NeverDestroyed types = HashSet<String> { "video/mp4"_s, "audio/mp4"_s, "video/webm"_s, "audio/webm"_s };
    return types;
}
#endif

// A codec string ("avc1.64001f", "vp09.00.10.08", "opus", ...) the decoders here take.
static bool isSupportedCodec(StringView codec)
{
    auto name = codec.trim(isASCIIWhitespace<char16_t>);
    auto dot = name.find('.');
    auto family = (dot == notFound ? name : name.left(dot)).convertToASCIILowercase();
    static constexpr ASCIILiteral supported[] = {
        "avc1"_s, "avc3"_s, "h264"_s, "mp4a"_s, "vp8"_s, "vp08"_s, "vp9"_s, "vp09"_s,
        "opus"_s, "vorbis"_s, "flac"_s, "mp3"_s, "1"_s,
    };
    for (auto entry : supported) {
        if (family == entry)
            return true;
    }
    // mp4a.40.2 (AAC), mp4a.6B / mp4a.69 (MP3) are all "mp4a" above; anything
    // else (av01, hev1, hvc1, ec-3, ...) has no decoder in this build.
    return false;
}

void MediaPlayerPrivateRitterOS::getSupportedTypes(HashSet<String>& types)
{
    types = fileTypes();
}

MediaPlayer::SupportsType MediaPlayerPrivateRitterOS::supportsType(const MediaEngineSupportParameters& parameters)
{
    auto container = parameters.type.containerType().convertToASCIILowercase();
    if (container.isEmpty())
        return MediaPlayer::SupportsType::IsNotSupported;
#if ENABLE(MEDIA_SOURCE)
    if (parameters.platformType == PlatformMediaDecodingType::MediaSource) {
        if (!mediaSourceTypes().contains(container))
            return MediaPlayer::SupportsType::IsNotSupported;
    } else
#endif
    if (!fileTypes().contains(container))
        return MediaPlayer::SupportsType::IsNotSupported;
    auto codecs = parameters.type.codecs();
    if (codecs.isEmpty())
        return MediaPlayer::SupportsType::MayBeSupported;
    for (auto& codec : codecs) {
        if (!isSupportedCodec(codec))
            return MediaPlayer::SupportsType::IsNotSupported;
    }
    return MediaPlayer::SupportsType::IsSupported;
}

// --- MediaByteSourceRitterOS: a file's bytes through WebKit's loader -------------

class MediaByteSourceRitterOS : public ThreadSafeRefCounted<MediaByteSourceRitterOS> {
public:
    static Ref<MediaByteSourceRitterOS> create(Ref<PlatformMediaResourceLoader>&& loader, const URL& url, const String& referrer)
    {
        return adoptRef(*new MediaByteSourceRitterOS(WTF::move(loader), url, referrer));
    }

    // Main thread: (re)start the download at offset (a Range request).
    void start(uint64_t offset);
    void abort();

    // The demuxing thread: FFmpeg's AVIOContext callbacks.
    int read(uint8_t*, int size);
    int64_t seek(int64_t offset, int whence);

    uint64_t loadedBytes() const
    {
        Locker locker { m_lock };
        return m_dataStart + m_data.size();
    }
    std::optional<uint64_t> totalSize() const
    {
        Locker locker { m_lock };
        return m_totalSize;
    }
    bool isComplete() const
    {
        Locker locker { m_lock };
        return m_complete && !m_dataStart;
    }
    bool hasFailed() const
    {
        Locker locker { m_lock };
        return m_failed;
    }

private:
    class Client;
    MediaByteSourceRitterOS(Ref<PlatformMediaResourceLoader>&& loader, const URL& url, const String& referrer)
        : m_loader(WTF::move(loader))
        , m_url(url)
        , m_referrer(referrer)
    {
    }

    void didReceiveResponse(unsigned request, const ResourceResponse&);
    void didReceiveData(unsigned request, const SharedBuffer&);
    void didFinish(unsigned request, bool failed);

    // Main thread only.
    const Ref<PlatformMediaResourceLoader> m_loader;
    RefPtr<PlatformMediaResource> m_resource;
    const URL m_url;
    const String m_referrer;

    mutable Lock m_lock;
    Condition m_condition;
    unsigned m_request WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    Vector<uint8_t> m_data WTF_GUARDED_BY_LOCK(m_lock); // the bytes from m_dataStart on
    uint64_t m_dataStart WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    bool m_restartPending WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_complete WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_failed WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_aborted WTF_GUARDED_BY_LOCK(m_lock) { false };
    std::optional<uint64_t> m_totalSize WTF_GUARDED_BY_LOCK(m_lock);
    uint64_t m_position { 0 }; // the demuxing thread's
};

class MediaByteSourceRitterOS::Client final : public PlatformMediaResourceClient {
public:
    Client(MediaByteSourceRitterOS& source, unsigned request)
        : m_source(source)
        , m_request(request)
    {
    }

private:
    void responseReceived(PlatformMediaResource&, const ResourceResponse& response, CompletionHandler<void(ShouldContinuePolicyCheck)>&& completionHandler) final
    {
        m_source->didReceiveResponse(m_request, response);
        completionHandler(ShouldContinuePolicyCheck::Yes);
    }
    bool shouldCacheResponse(PlatformMediaResource&, const ResourceResponse&) final { return false; }
    void dataReceived(PlatformMediaResource&, const SharedBuffer& buffer) final { m_source->didReceiveData(m_request, buffer); }
    void accessControlCheckFailed(PlatformMediaResource&, const ResourceError&) final { m_source->didFinish(m_request, true); }
    void loadFailed(PlatformMediaResource&, const ResourceError&) final { m_source->didFinish(m_request, true); }
    void loadFinished(PlatformMediaResource&, const NetworkLoadMetrics&) final { m_source->didFinish(m_request, false); }

    const Ref<MediaByteSourceRitterOS> m_source;
    const unsigned m_request;
};

void MediaByteSourceRitterOS::start(uint64_t offset)
{
    ASSERT(isMainThread());
    if (RefPtr resource = std::exchange(m_resource, nullptr)) {
        resource->setClient(nullptr);
        resource->shutdown();
    }
    unsigned request;
    {
        Locker locker { m_lock };
        if (m_aborted)
            return;
        request = ++m_request;
        m_data.clear();
        m_dataStart = offset;
        m_complete = false;
        m_failed = false;
        m_restartPending = false;
    }
    ResourceRequest resourceRequest { URL { m_url } };
    resourceRequest.setAllowCookies(true);
    if (!m_referrer.isEmpty())
        resourceRequest.setHTTPReferrer(m_referrer);
    if (offset)
        resourceRequest.setHTTPHeaderField(HTTPHeaderName::Range, makeString("bytes="_s, offset, '-'));
    m_resource = m_loader->requestResource(WTF::move(resourceRequest), PlatformMediaResourceLoader::LoadOption::DisallowCaching);
    if (!m_resource) {
        didFinish(request, true);
        return;
    }
    m_resource->setClient(adoptRef(*new Client(*this, request)));
}

void MediaByteSourceRitterOS::abort()
{
    {
        Locker locker { m_lock };
        m_aborted = true;
        m_condition.notifyAll();
    }
    if (RefPtr resource = std::exchange(m_resource, nullptr)) {
        resource->setClient(nullptr);
        resource->shutdown();
    }
}

void MediaByteSourceRitterOS::didReceiveResponse(unsigned request, const ResourceResponse& response)
{
    Locker locker { m_lock };
    if (request != m_request)
        return;
    int status = response.httpStatusCode();
    if (status >= 400) {
        m_failed = true;
        m_condition.notifyAll();
        return;
    }
    if (status == 206) {
        // Content-Range: bytes FIRST-LAST/TOTAL
        auto range = response.httpHeaderField(HTTPHeaderName::ContentRange);
        auto slash = range.reverseFind('/');
        if (slash != notFound) {
            if (auto total = parseInteger<uint64_t>(StringView(range).substring(slash + 1)))
                m_totalSize = *total;
        }
    } else {
        // (A server that ignores Range sends it all from the start.)
        m_dataStart = 0;
        if (response.expectedContentLength() > 0)
            m_totalSize = response.expectedContentLength();
    }
}

void MediaByteSourceRitterOS::didReceiveData(unsigned request, const SharedBuffer& buffer)
{
    Locker locker { m_lock };
    if (request != m_request)
        return;
    m_data.append(buffer.span());
    m_condition.notifyAll();
}

void MediaByteSourceRitterOS::didFinish(unsigned request, bool failed)
{
    Locker locker { m_lock };
    if (request != m_request)
        return;
    if (failed)
        m_failed = true;
    else {
        m_complete = true;
        if (!m_totalSize || !m_dataStart)
            m_totalSize = m_dataStart + m_data.size();
    }
    m_condition.notifyAll();
}

int MediaByteSourceRitterOS::read(uint8_t* buffer, int size)
{
    // A read this far past what has come waits for it; further restarts the
    // download there.
    static constexpr uint64_t waitWindow = 2 * 1024 * 1024;
    Locker locker { m_lock };
    while (true) {
        if (m_aborted)
            return AVERROR_EXIT;
        uint64_t end = m_dataStart + m_data.size();
        if (m_position >= m_dataStart && m_position < end) {
            size_t count = std::min<uint64_t>(size, end - m_position);
            memcpy(buffer, m_data.span().subspan(m_position - m_dataStart).data(), count);
            m_position += count;
            return static_cast<int>(count);
        }
        if (m_totalSize && m_position >= *m_totalSize)
            return AVERROR_EOF;
        if (m_position == end && m_complete)
            return AVERROR_EOF;
        if (m_failed && !m_restartPending)
            return AVERROR(EIO);
        bool coming = m_position >= m_dataStart && m_position < end + waitWindow && !m_complete && !m_failed;
        if (!coming && !m_restartPending) {
            m_restartPending = true;
            callOnMainThread([source = Ref { *this }, position = m_position] {
                source->start(position);
            });
        }
        m_condition.waitFor(m_lock, 100_ms);
    }
}

int64_t MediaByteSourceRitterOS::seek(int64_t offset, int whence)
{
    Locker locker { m_lock };
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE:
        return m_totalSize ? static_cast<int64_t>(*m_totalSize) : -1;
    case SEEK_SET:
        break;
    case SEEK_CUR:
        offset += m_position;
        break;
    case SEEK_END:
        if (!m_totalSize)
            return -1;
        offset += *m_totalSize;
        break;
    default:
        return -1;
    }
    if (offset < 0)
        return -1;
    m_position = offset;
    return offset;
}

// --- The demuxing thread -----------------------------------------------------------

class MediaDemuxControlRitterOS : public ThreadSafeRefCounted<MediaDemuxControlRitterOS> {
public:
    static Ref<MediaDemuxControlRitterOS> create() { return adoptRef(*new MediaDemuxControlRitterOS); }

    void stop()
    {
        Locker locker { lock };
        stopping = true;
        condition.notifyAll();
    }
    void seek(const MediaTime& time)
    {
        Locker locker { lock };
        seekTo = time;
        condition.notifyAll();
    }

    Lock lock;
    Condition condition;
    bool stopping WTF_GUARDED_BY_LOCK(lock) { false };
    std::optional<MediaTime> seekTo WTF_GUARDED_BY_LOCK(lock);
};

static MediaTime toMediaTime(int64_t value, AVRational timeBase)
{
    if (value == AV_NOPTS_VALUE)
        return MediaTime::invalidTime();
    return MediaTime(av_rescale_q(value, timeBase, AVRational { 1, 1000000 }), 1000000);
}

static int readCallback(void* opaque, uint8_t* buffer, int size)
{
    return static_cast<MediaByteSourceRitterOS*>(opaque)->read(buffer, size);
}

static int64_t seekCallback(void* opaque, int64_t offset, int whence)
{
    return static_cast<MediaByteSourceRitterOS*>(opaque)->seek(offset, whence);
}

static void demuxThreadMain(Ref<MediaByteSourceRitterOS> source, Ref<MediaPipelineRitterOS> pipeline,
    Ref<MediaDemuxControlRitterOS> control, ThreadSafeWeakPtr<MediaPlayerPrivateRitterOS> weakPlayer)
{
    auto post = [&](Function<void(MediaPlayerPrivateRitterOS&)>&& task) {
        callOnMainThread([weakPlayer, task = WTF::move(task)] {
            if (RefPtr player = weakPlayer.get())
                task(*player);
        });
    };

    constexpr int bufferSize = 64 * 1024;
    auto* io = avio_alloc_context(static_cast<uint8_t*>(av_malloc(bufferSize)), bufferSize, 0, source.ptr(), readCallback, nullptr, seekCallback);
    auto* format = avformat_alloc_context();
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;

    auto finish = [&] {
        avformat_close_input(&format);
        if (io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
    };

    if (avformat_open_input(&format, nullptr, nullptr, nullptr) < 0 || avformat_find_stream_info(format, nullptr) < 0) {
        bool network = source->hasFailed();
        post([network](auto& player) {
            player.didFail(network ? MediaPlayer::NetworkState::NetworkError : MediaPlayer::NetworkState::FormatError);
        });
        finish();
        return;
    }

    int videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int audioIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    auto usable = [&](int index) {
        return index >= 0 && avcodec_find_decoder(format->streams[index]->codecpar->codec_id)
            && !(format->streams[index]->disposition & AV_DISPOSITION_ATTACHED_PIC);
    };
    if (!usable(videoIndex))
        videoIndex = -1;
    if (!usable(audioIndex))
        audioIndex = -1;
    if (videoIndex < 0 && audioIndex < 0) {
        post([](auto& player) { player.didFail(MediaPlayer::NetworkState::FormatError); });
        finish();
        return;
    }

    RefPtr<MediaCodecConfigRitterOS> videoConfig, audioConfig;
    MediaPlayerPrivateRitterOS::Metadata metadata;
    if (videoIndex >= 0) {
        auto* stream = format->streams[videoIndex];
        videoConfig = MediaCodecConfigRitterOS::create(stream->codecpar, stream->time_base.num, stream->time_base.den);
        metadata.hasVideo = true;
        metadata.naturalSize = FloatSize(videoConfig->width(), videoConfig->height());
    }
    if (audioIndex >= 0) {
        auto* stream = format->streams[audioIndex];
        audioConfig = MediaCodecConfigRitterOS::create(stream->codecpar, stream->time_base.num, stream->time_base.den);
        metadata.hasAudio = true;
    }
    metadata.duration = format->duration > 0 ? MediaTime(format->duration, AV_TIME_BASE) : MediaTime::positiveInfiniteTime();
    pipeline->setHasVideo(metadata.hasVideo);
    pipeline->setHasAudio(metadata.hasAudio);
    post([metadata](auto& player) { player.didLoadMetadata(metadata); });

    AVPacket* packet = av_packet_alloc();
    bool atEnd = false;
    while (true) {
        std::optional<MediaTime> seekTo;
        {
            Locker locker { control->lock };
            if (atEnd && !control->stopping && !control->seekTo)
                control->condition.waitFor(control->lock, 200_ms);
            if (control->stopping)
                break;
            seekTo = std::exchange(control->seekTo, std::nullopt);
        }
        if (seekTo) {
            int64_t target = llround(seekTo->toDouble() * AV_TIME_BASE);
            avformat_seek_file(format, -1, INT64_MIN, target, target, AVSEEK_FLAG_BACKWARD);
            pipeline->flush(*seekTo);
            atEnd = false;
            post([time = *seekTo](auto& player) { player.didSeek(time); });
            continue;
        }
        if (atEnd)
            continue;

        int error = av_read_frame(format, packet);
        if (error < 0) {
            if (error == AVERROR_EXIT)
                break;
            // The end (or a read that failed: what there is plays out).
            if (videoIndex >= 0)
                pipeline->endOfVideo();
            if (audioIndex >= 0)
                pipeline->endOfAudio();
            atEnd = true;
            pipeline->wakeMainThread();
            continue;
        }
        bool isVideo = packet->stream_index == videoIndex;
        if (!isVideo && packet->stream_index != audioIndex) {
            av_packet_unref(packet);
            continue;
        }
        auto* stream = format->streams[packet->stream_index];
        MediaPacketRitterOS media;
        media.config = isVideo ? videoConfig : audioConfig;
        media.data = Vector<uint8_t>(std::span<const uint8_t>(packet->data, packet->size));
        media.presentationTime = toMediaTime(packet->pts, stream->time_base);
        media.decodeTime = toMediaTime(packet->dts, stream->time_base);
        media.duration = toMediaTime(packet->duration, stream->time_base);
        media.isKeyFrame = packet->flags & AV_PKT_FLAG_KEY;
        if (!media.presentationTime.isValid())
            media.presentationTime = media.decodeTime;
        av_packet_unref(packet);

        // Wait for room (a seek or the end of it all comes first).
        while (!(isVideo ? pipeline->pushVideo(WTF::move(media)) : pipeline->pushAudio(WTF::move(media)))) {
            Locker locker { control->lock };
            if (control->stopping || control->seekTo)
                break;
            control->condition.waitFor(control->lock, 10_ms);
        }
    }
    av_packet_free(&packet);
    finish();
}

// --- MediaPlayerPrivateRitterOS ----------------------------------------------------------

MediaPlayerPrivateRitterOS::MediaPlayerPrivateRitterOS(MediaPlayer& player)
    : m_player(player)
    , m_pipeline(MediaPipelineRitterOS::create())
    , m_tickTimer(RunLoop::mainSingleton(), "MediaPlayerPrivateRitterOS::Tick"_s, this, &MediaPlayerPrivateRitterOS::tick)
{
}

MediaPlayerPrivateRitterOS::~MediaPlayerPrivateRitterOS()
{
    stopLoading();
    m_pipeline->stop();
}

void MediaPlayerPrivateRitterOS::wake()
{
    if (m_wakePending.exchange(true))
        return;
    callOnMainThread([weakThis = ThreadSafeWeakPtr { *this }] {
        if (RefPtr player = weakThis.get()) {
            player->m_wakePending = false;
            player->tick();
        }
    });
}

void MediaPlayerPrivateRitterOS::load(const String& urlString)
{
    RefPtr player = m_player.get();
    if (!player)
        return;
    m_pipeline->setWakeMainThread([weakThis = ThreadSafeWeakPtr { *this }] {
        if (RefPtr player = weakThis.get())
            player->wake();
    });
    setNetworkState(MediaPlayer::NetworkState::Loading);
    setReadyStateInternal(MediaPlayer::ReadyState::HaveNothing);
    m_source = MediaByteSourceRitterOS::create(player->mediaResourceLoader(), URL { urlString }, player->referrer());
    m_source->start(0);
    m_demux = MediaDemuxControlRitterOS::create();
    Thread::create("Media demux"_s, [source = Ref { *m_source }, pipeline = m_pipeline, control = Ref { *m_demux }, weakThis = ThreadSafeWeakPtr { *this }]() mutable {
        demuxThreadMain(WTF::move(source), WTF::move(pipeline), WTF::move(control), WTF::move(weakThis));
    })->detach();
}

#if ENABLE(MEDIA_SOURCE)
void MediaPlayerPrivateRitterOS::load(const URL&, const LoadOptions&, MediaSourcePrivateClient& client)
{
    m_isMediaSource = true;
    m_pipeline->setWakeMainThread([weakThis = ThreadSafeWeakPtr { *this }] {
        if (RefPtr player = weakThis.get())
            player->wake();
    });
    if (RefPtr mediaSourcePrivate = dynamicDowncast<MediaSourcePrivateRitterOS>(client.mediaSourcePrivate())) {
        mediaSourcePrivate->setPlayer(this);
        m_mediaSourcePrivate = WTF::move(mediaSourcePrivate);
        client.reOpen();
    } else
        m_mediaSourcePrivate = MediaSourcePrivateRitterOS::create(*this, client);
}

void MediaPlayerPrivateRitterOS::readyStateFromMediaSourceChanged()
{
    if (RefPtr player = m_player.get())
        player->readyStateChanged();
}

void MediaPlayerPrivateRitterOS::characteristicsFromMediaSourceChanged()
{
    if (m_mediaSourcePrivate) {
        m_hasVideo = m_mediaSourcePrivate->hasVideo();
        m_hasAudio = m_mediaSourcePrivate->hasAudio();
        m_pipeline->setHasVideo(m_hasVideo);
        m_pipeline->setHasAudio(m_hasAudio);
    }
    if (RefPtr player = m_player.get())
        player->characteristicChanged();
}

void MediaPlayerPrivateRitterOS::seekableRangesFromMediaSourceChanged()
{
    if (RefPtr player = m_player.get())
        player->seekableTimeRangesChanged();
}

void MediaPlayerPrivateRitterOS::notifyActiveSourceBuffersChanged()
{
    if (RefPtr player = m_player.get())
        player->activeSourceBuffersChanged();
}

void MediaPlayerPrivateRitterOS::mediaSourceHasRetrievedAllData()
{
    setNetworkState(MediaPlayer::NetworkState::Loaded);
}

void MediaPlayerPrivateRitterOS::durationFromMediaSourceChanged()
{
    if (RefPtr player = m_player.get())
        player->durationChanged();
}

void MediaPlayerPrivateRitterOS::mediaSourceVideoSizeChanged(const FloatSize& size)
{
    if (size == m_naturalSize || size.isEmpty())
        return;
    m_naturalSize = size;
    if (RefPtr player = m_player.get())
        player->sizeChanged();
}

void MediaPlayerPrivateRitterOS::seekMediaSource(const SeekTarget& target)
{
    // As the mock engine: wait for the MediaSource to have the time, then
    // play from it (the pipeline is emptied, the SourceBuffers enqueue again
    // from the sync sample before it).
    RefPtr mediaSourcePrivate = m_mediaSourcePrivate;
    if (!mediaSourcePrivate)
        return;
    mediaSourcePrivate->waitForTarget(target)->whenSettled(RunLoop::mainSingleton(), [weakThis = ThreadSafeWeakPtr { *this }](auto&& result) {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;
        if (!result) {
            if (auto promise = std::exchange(protectedThis->m_seekPromise, std::nullopt))
                promise->reject(result.error());
            return;
        }
        auto time = *result;
        protectedThis->m_pipeline->flush(time);
        RefPtr mediaSourcePrivate = protectedThis->m_mediaSourcePrivate;
        if (!mediaSourcePrivate)
            return;
        mediaSourcePrivate->reenqueueMediaForTime(time)->whenSettled(RunLoop::mainSingleton(), [weakThis, time](auto&& result) {
            RefPtr protectedThis = weakThis.get();
            if (!protectedThis || !result)
                return;
            protectedThis->m_seekTime.reset();
            if (auto promise = std::exchange(protectedThis->m_seekPromise, std::nullopt))
                promise->resolve(time);
            protectedThis->tick();
        });
    });
}
#endif

void MediaPlayerPrivateRitterOS::stopLoading()
{
    if (RefPtr demux = std::exchange(m_demux, nullptr))
        demux->stop();
    if (RefPtr source = std::exchange(m_source, nullptr))
        source->abort();
}

void MediaPlayerPrivateRitterOS::cancelLoad()
{
    stopLoading();
    m_pipeline->setPlaying(false);
    m_tickTimer.stop();
}

void MediaPlayerPrivateRitterOS::didLoadMetadata(const Metadata& metadata)
{
    RefPtr player = m_player.get();
    m_hasVideo = metadata.hasVideo;
    m_hasAudio = metadata.hasAudio;
    m_naturalSize = metadata.naturalSize;
    m_duration = metadata.duration;
    if (!player)
        return;
    player->characteristicChanged();
    player->durationChanged();
    if (m_hasVideo)
        player->sizeChanged();
    setReadyStateInternal(MediaPlayer::ReadyState::HaveMetadata);
    if (!m_hasVideo)
        setReadyStateInternal(MediaPlayer::ReadyState::HaveEnoughData);
    tick();
}

void MediaPlayerPrivateRitterOS::didFail(MediaPlayer::NetworkState state)
{
    m_pipeline->setPlaying(false);
    m_tickTimer.stop();
    setNetworkState(state);
}

void MediaPlayerPrivateRitterOS::didSeek(const MediaTime& time)
{
    if (!m_seekTime || *m_seekTime != time)
        return;
    m_seekTime.reset();
    if (auto promise = std::exchange(m_seekPromise, std::nullopt))
        promise->resolve(time);
    tick();
}

void MediaPlayerPrivateRitterOS::setNetworkState(MediaPlayer::NetworkState state)
{
    if (m_networkState == state)
        return;
    m_networkState = state;
    if (RefPtr player = m_player.get())
        player->networkStateChanged();
}

void MediaPlayerPrivateRitterOS::setReadyStateInternal(MediaPlayer::ReadyState state)
{
    if (m_readyState == state)
        return;
    m_readyState = state;
    if (RefPtr player = m_player.get())
        player->readyStateChanged();
}

MediaPlayer::ReadyState MediaPlayerPrivateRitterOS::readyState() const
{
#if ENABLE(MEDIA_SOURCE)
    if (m_isMediaSource)
        return m_mediaSourcePrivate ? m_mediaSourcePrivate->mediaPlayerReadyState() : MediaPlayer::ReadyState::HaveNothing;
#endif
    return m_readyState;
}

void MediaPlayerPrivateRitterOS::tick()
{
    RefPtr player = m_player.get();
    if (!player)
        return;
#if ENABLE(MEDIA_SOURCE)
    if (m_mediaSourcePrivate)
        m_mediaSourcePrivate->provideMoreSamplesIfWaiting();
#endif
    bool hadFirstImage = m_pipeline->hasFirstImage();
    if (m_pipeline->updateCurrentImage()) {
        if (m_isMediaSource) {
            if (!hadFirstImage)
                player->firstVideoFrameAvailable();
        } else if (m_readyState < MediaPlayer::ReadyState::HaveEnoughData && m_readyState >= MediaPlayer::ReadyState::HaveMetadata) {
            setReadyStateInternal(MediaPlayer::ReadyState::HaveEnoughData);
            player->firstVideoFrameAvailable();
        }
        player->repaint();
    }
    if (!m_isMediaSource && m_readyState >= MediaPlayer::ReadyState::HaveCurrentData) {
        bool stalled = !m_paused && m_pipeline->isStalled();
        setReadyStateInternal(stalled ? MediaPlayer::ReadyState::HaveCurrentData : MediaPlayer::ReadyState::HaveEnoughData);
    }
    if (m_source) {
        if (m_source->isComplete())
            setNetworkState(MediaPlayer::NetworkState::Loaded);
        else if (m_source->hasFailed() && m_networkState == MediaPlayer::NetworkState::Loading)
            setNetworkState(MediaPlayer::NetworkState::Idle);
    }
    if (!m_paused && !m_ended && !m_seekTime && m_pipeline->isEnded()) {
        m_ended = true;
        m_pipeline->setPlaying(false);
        m_tickTimer.stop();
        player->timeChanged();
    }
    if (m_paused || m_ended)
        return;

    static bool statistics = getenv("COMPASS_MEDIA_STATS");
    if (statistics && MonotonicTime::now() - m_lastStatistics > 5_s) {
        m_lastStatistics = MonotonicTime::now();
        m_pipeline->reportStatistics();
    }
    // The next look: when the next picture is due (a picture shown on time,
    // not at the next turn of a fixed timer), at most 50 ms on (the sound's
    // end, a stall's end; the decoding threads also wake this up).
    auto delay = 50_ms;
    if (auto untilNext = m_pipeline->timeUntilNextPicture())
        delay = std::clamp(*untilNext, 0_s, 50_ms);
    m_tickTimer.startOneShot(delay);
}

void MediaPlayerPrivateRitterOS::play()
{
    m_paused = false;
    if (m_ended) {
        // (HTMLMediaElement seeks to the start before playing again.)
        m_ended = false;
    }
    m_pipeline->setPlaying(true);
    tick(); // (it sets the timer)
}

void MediaPlayerPrivateRitterOS::pause()
{
    m_paused = true;
    m_pipeline->setPlaying(false);
    m_tickTimer.stop();
}

MediaTime MediaPlayerPrivateRitterOS::duration() const
{
#if ENABLE(MEDIA_SOURCE)
    if (m_isMediaSource)
        return m_mediaSourcePrivate ? m_mediaSourcePrivate->duration() : MediaTime::zeroTime();
#endif
    return m_duration.isValid() ? m_duration : MediaTime::zeroTime();
}

MediaTime MediaPlayerPrivateRitterOS::currentTime() const
{
    if (m_seekTime)
        return *m_seekTime;
    auto end = duration();
    // Played to the end: the end (HTMLMediaElement's "ended" is currentTime
    // at the duration; the last sound can end a little before a MediaSource's
    // duration, the end of what was buffered).
    if (m_ended && end.isValid() && end.isFinite())
        return end;
    auto time = m_pipeline->currentTime();
    if (end.isValid() && end.isFinite() && end > MediaTime::zeroTime() && time > end)
        return end;
    return time;
}

bool MediaPlayerPrivateRitterOS::timeIsProgressing() const
{
    return !m_paused && !m_ended && !m_seekTime && !m_pipeline->isStalled();
}

Ref<MediaTimePromise> MediaPlayerPrivateRitterOS::seekToTarget(const SeekTarget& target)
{
    m_seekPromise.emplace(PlatformMediaError::Cancelled);
    m_seekTime = target.time;
    // From the end (the pipeline stopped there) while not paused: `loop`'s
    // seek to the start plays on.
    if (std::exchange(m_ended, false) && !m_paused)
        m_pipeline->setPlaying(true);
#if ENABLE(MEDIA_SOURCE)
    if (m_isMediaSource) {
        Ref<MediaTimePromise> promise = *m_seekPromise;
        seekMediaSource(target);
        return promise;
    }
#endif
    if (m_demux)
        m_demux->seek(target.time);
    return *m_seekPromise;
}

void MediaPlayerPrivateRitterOS::setVolumeDouble(double volume)
{
    m_pipeline->setVolume(static_cast<float>(volume));
}

void MediaPlayerPrivateRitterOS::setMuted(bool muted)
{
    m_pipeline->setMuted(muted);
}

MediaTime MediaPlayerPrivateRitterOS::maxTimeSeekable() const
{
#if ENABLE(MEDIA_SOURCE)
    if (m_isMediaSource)
        return duration();
#endif
    if (!m_duration.isValid() || !m_duration.isFinite())
        return MediaTime::zeroTime();
    return m_duration;
}

const PlatformTimeRanges& MediaPlayerPrivateRitterOS::buffered() const
{
#if ENABLE(MEDIA_SOURCE)
    if (m_isMediaSource && m_mediaSourcePrivate) {
        m_buffered = m_mediaSourcePrivate->buffered();
        return m_buffered;
    }
#endif
    m_buffered = { };
    if (!m_source || !m_duration.isValid() || !m_duration.isFinite())
        return m_buffered;
    // Bytes to time, as if the file were even: good enough for the bar.
    auto total = m_source->totalSize();
    if (m_source->isComplete())
        m_buffered.add(MediaTime::zeroTime(), m_duration);
    else if (total && *total)
        m_buffered.add(MediaTime::zeroTime(), MediaTime::createWithDouble(m_duration.toDouble() * std::min<double>(1, static_cast<double>(m_source->loadedBytes()) / *total)));
    return m_buffered;
}

bool MediaPlayerPrivateRitterOS::didLoadingProgress() const
{
    if (!m_source)
        return false;
    auto loaded = m_source->loadedBytes();
    bool progressed = loaded != m_lastLoadedBytes;
    m_lastLoadedBytes = loaded;
    return progressed;
}

unsigned long long MediaPlayerPrivateRitterOS::totalBytes() const
{
    if (!m_source)
        return 0;
    return m_source->totalSize().value_or(0);
}

// The picture covers rect; where the page is only moved and scaled (no
// rotation or skew), that is a rectangle of the screen's pixels: the
// pipeline converts the next pictures to its size, and a picture of that
// size is copied there as it is, in the screen's pixels (no filtering:
// Skia's plain copy). Another size (the first pictures after a resize,
// a transformed page) is stretched as before.
void MediaPlayerPrivateRitterOS::paint(GraphicsContext& context, const FloatRect& rect)
{
    if (context.paintingDisabled())
        return;
    RefPtr image = m_pipeline->currentImage();
    if (!image)
        return;
    auto ctm = context.getCTM(GraphicsContext::DefinitelyIncludeDeviceScale);
    if (!ctm.b() && !ctm.c() && ctm.a() > 0 && ctm.d() > 0) {
        auto onScreen = roundedIntRect(ctm.mapRect(rect));
        if (!onScreen.isEmpty()) {
            m_pipeline->setDisplaySize(onScreen.size());
            if (image->size() == onScreen.size()) {
                GraphicsContextStateSaver saver(context);
                context.setCTM({ });
                context.drawNativeImage(*image, FloatRect(onScreen), FloatRect({ }, image->size()), { InterpolationQuality::DoNotInterpolate });
                return;
            }
        }
    }
    context.drawNativeImage(*image, rect, FloatRect({ }, image->size()));
}

// (canvas's drawImage: the video's own size, which it takes for the source)
RefPtr<NativeImage> MediaPlayerPrivateRitterOS::nativeImageForCurrentTime()
{
    return m_pipeline->currentNaturalImage();
}

ColorSpace MediaPlayerPrivateRitterOS::colorSpace()
{
    return ColorSpace::SRGB();
}

unsigned MediaPlayerPrivateRitterOS::decodedFrameCount() const
{
    return m_pipeline->decodedFrameCount();
}

unsigned MediaPlayerPrivateRitterOS::droppedFrameCount() const
{
    return m_pipeline->droppedFrameCount();
}

std::optional<VideoPlaybackQualityMetrics> MediaPlayerPrivateRitterOS::videoPlaybackQualityMetrics()
{
    VideoPlaybackQualityMetrics metrics;
    metrics.totalVideoFrames = m_pipeline->decodedFrameCount();
    metrics.droppedVideoFrames = m_pipeline->droppedFrameCount();
    return metrics;
}

} // namespace WebCore

#endif

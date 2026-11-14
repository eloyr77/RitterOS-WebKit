/*
 * RitterOS: WebKit's media engine for <video> and <audio> (MediaPlayer's
 * only engine on RitterOS). FFmpeg demuxes and decodes on the CPU; the
 * pictures are painted with the page (no compositing), the sound goes to
 * the sound device (MediaPipelineRitterOS, AudioOutputRitterOS).
 *
 * A file (src=URL) comes through WebKit's own loader (cookies, the
 * page's referrer), with Range requests when the demuxer seeks
 * (MediaByteSourceRitterOS), and a demuxing thread feeds the pipeline.
 * With Media Source Extensions the page's SourceBuffers feed it instead
 * (MediaSourcePrivateRitterOS).
 */

#pragma once

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include "MediaPlayerPrivate.h"
#include "PlatformTimeRanges.h"
#include <wtf/Lock.h>
#include <wtf/MediaTime.h>
#include <wtf/NativePromise.h>
#include <wtf/RunLoop.h>
#include <wtf/ThreadSafeWeakPtr.h>

namespace WebCore {

class MediaByteSourceRitterOS;
class MediaDemuxControlRitterOS;
class MediaPipelineRitterOS;
#if ENABLE(MEDIA_SOURCE)
class MediaSourcePrivateRitterOS;
#endif

class MediaPlayerPrivateRitterOS final
    : public MediaPlayerPrivateInterface
    , public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<MediaPlayerPrivateRitterOS, WTF::DestructionThread::Main> {
public:
    void ref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::ref(); }
    void deref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::deref(); }

    static void registerMediaEngine(MediaEngineRegistrar);
    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);

    explicit MediaPlayerPrivateRitterOS(MediaPlayer&);
    ~MediaPlayerPrivateRitterOS();

    constexpr MediaPlayerType mediaPlayerType() const final { return MediaPlayerType::RitterOS; }

    MediaPipelineRitterOS& pipeline() { return m_pipeline.get(); }

    // From the demuxing thread (on the main thread) and MSE.
    struct Metadata {
        MediaTime duration;
        FloatSize naturalSize;
        bool hasVideo { false };
        bool hasAudio { false };
    };
    void didLoadMetadata(const Metadata&);
    void didFail(MediaPlayer::NetworkState);
    void didSeek(const MediaTime&);
    void wake();

#if ENABLE(MEDIA_SOURCE)
    // MSE: the MediaSource's tracks and readiness.
    void mediaSourceVideoSizeChanged(const FloatSize&);
    void durationFromMediaSourceChanged();
    void readyStateFromMediaSourceChanged() final;
    void characteristicsFromMediaSourceChanged() final;
    void seekableRangesFromMediaSourceChanged() final;
    void notifyActiveSourceBuffersChanged() final;
    void mediaSourceHasRetrievedAllData() final;
#endif

private:
    void load(const String&) final;
#if ENABLE(MEDIA_SOURCE)
    void load(const URL&, const LoadOptions&, MediaSourcePrivateClient&) final;
#endif
    void cancelLoad() final;
    void prepareToPlay() final { }
    void play() final;
    void pause() final;
    bool paused() const final { return m_paused; }
    FloatSize naturalSize() const final { return m_naturalSize; }
    bool hasVideo() const final { return m_hasVideo; }
    bool hasAudio() const final { return m_hasAudio; }
    void setPageIsVisible(bool) final { }
    MediaTime duration() const final;
    MediaTime currentTime() const final;
    bool timeIsProgressing() const final;
    Ref<MediaTimePromise> seekToTarget(const SeekTarget&) final;
    double rate() const final { return m_paused ? 0 : 1; }
    void setVolumeDouble(double) final;
    void setMuted(bool) final;
    MediaPlayer::NetworkState networkState() const final { return m_networkState; }
    MediaPlayer::ReadyState readyState() const final;
    MediaTime maxTimeSeekable() const final;
    const PlatformTimeRanges& buffered() const final;
    bool didLoadingProgress() const final;
    unsigned long long totalBytes() const final;
    void paint(GraphicsContext&, const FloatRect&) final;
    RefPtr<NativeImage> nativeImageForCurrentTime() final;
    ColorSpace colorSpace() final;
    String engineDescription() const final { return "RitterOS (FFmpeg)"_s; }
    bool ended() const final { return m_ended; }
    unsigned decodedFrameCount() const final;
    unsigned droppedFrameCount() const final;
    std::optional<VideoPlaybackQualityMetrics> videoPlaybackQualityMetrics() final;

    void tick();
    void setNetworkState(MediaPlayer::NetworkState);
    void setReadyStateInternal(MediaPlayer::ReadyState);
    void stopLoading();
#if ENABLE(MEDIA_SOURCE)
    void seekMediaSource(const SeekTarget&);
#endif

    ThreadSafeWeakPtr<MediaPlayer> m_player;
    const Ref<MediaPipelineRitterOS> m_pipeline;
    RefPtr<MediaByteSourceRitterOS> m_source;
    RefPtr<MediaDemuxControlRitterOS> m_demux;
#if ENABLE(MEDIA_SOURCE)
    RefPtr<MediaSourcePrivateRitterOS> m_mediaSourcePrivate;
#endif
    RunLoop::Timer m_tickTimer;
    std::atomic<bool> m_wakePending { false };

    MediaPlayer::NetworkState m_networkState { MediaPlayer::NetworkState::Empty };
    MediaPlayer::ReadyState m_readyState { MediaPlayer::ReadyState::HaveNothing };
    MediaTime m_duration { MediaTime::invalidTime() };
    FloatSize m_naturalSize;
    bool m_hasVideo { false };
    bool m_hasAudio { false };
    bool m_paused { true };
    bool m_ended { false };
    bool m_isMediaSource { false };
    std::optional<MediaTime> m_seekTime;
    std::optional<MediaTimePromise::AutoRejectProducer> m_seekPromise;
    mutable PlatformTimeRanges m_buffered;
    mutable uint64_t m_lastLoadedBytes { 0 };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::MediaPlayerPrivateRitterOS)
static bool isType(const WebCore::MediaPlayerPrivateInterface& player) { return player.mediaPlayerType() == WebCore::MediaPlayerType::RitterOS; }
SPECIALIZE_TYPE_TRAITS_END()

#endif

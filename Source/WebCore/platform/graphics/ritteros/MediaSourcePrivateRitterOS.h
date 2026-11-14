/*
 * RitterOS: Media Source Extensions for the media engine. The page's
 * MediaSource and its SourceBuffers (SourceBufferPrivateRitterOS) feed the
 * player's pipeline instead of a file's demuxer; WebKit's generic
 * MediaSourcePrivate/SourceBufferPrivate keep the samples, the buffered
 * ranges and the order they are played in.
 */

#pragma once

#if ENABLE(MEDIA_SOURCE) && PLATFORM(RITTEROS)

#include "MediaSourcePrivate.h"
#include <wtf/ThreadSafeWeakPtr.h>

namespace WebCore {

class MediaPlayerPrivateRitterOS;
class SourceBufferPrivateRitterOS;

class MediaSourcePrivateRitterOS final : public MediaSourcePrivate {
public:
    static Ref<MediaSourcePrivateRitterOS> create(MediaPlayerPrivateRitterOS&, MediaSourcePrivateClient&);
    ~MediaSourcePrivateRitterOS();

    constexpr MediaPlatformType platformType() const final { return MediaPlatformType::RitterOS; }
    RefPtr<MediaPlayerPrivateInterface> player() const final;
    void setPlayer(MediaPlayerPrivateInterface*) final;
    RefPtr<MediaPlayerPrivateRitterOS> ritterosPlayer() const { return m_player.get(); }

    // The pipeline has room again: SourceBuffers waiting to enqueue go on.
    void provideMoreSamplesIfWaiting();
    // A SourceBuffer's video track's size (from its initialization segment).
    void videoSizeChanged(const FloatSize&);

private:
    MediaSourcePrivateRitterOS(MediaPlayerPrivateRitterOS&, MediaSourcePrivateClient&);

    AddStatus addSourceBuffer(const ContentType&, const MediaSourceConfiguration&, RefPtr<SourceBufferPrivate>&) final;
    void durationChanged(const MediaTime&) final;
    void markEndOfStream(EndOfStreamStatus) final;
    void unmarkEndOfStream() final;
    void notifyActiveSourceBuffersChanged() final;

    ThreadSafeWeakPtr<MediaPlayerPrivateRitterOS> m_player;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::MediaSourcePrivateRitterOS)
static bool isType(const WebCore::MediaSourcePrivate& mediaSource) { return mediaSource.platformType() == WebCore::MediaPlatformType::RitterOS; }
SPECIALIZE_TYPE_TRAITS_END()

#endif

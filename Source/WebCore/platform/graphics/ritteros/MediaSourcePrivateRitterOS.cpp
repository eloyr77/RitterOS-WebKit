/*
 * RitterOS: Media Source Extensions for the media engine
 * (MediaSourcePrivateRitterOS.h).
 */

#include "config.h"
#include "MediaSourcePrivateRitterOS.h"

#if ENABLE(MEDIA_SOURCE) && PLATFORM(RITTEROS)

#include "ContentType.h"
#include "MediaPlayerPrivateRitterOS.h"
#include "MediaSourcePrivateClient.h"
#include "SourceBufferPrivateRitterOS.h"

namespace WebCore {

Ref<MediaSourcePrivateRitterOS> MediaSourcePrivateRitterOS::create(MediaPlayerPrivateRitterOS& player, MediaSourcePrivateClient& client)
{
    Ref source = adoptRef(*new MediaSourcePrivateRitterOS(player, client));
    client.setPrivateAndOpen(source.copyRef());
    return source;
}

MediaSourcePrivateRitterOS::MediaSourcePrivateRitterOS(MediaPlayerPrivateRitterOS& player, MediaSourcePrivateClient& client)
    : MediaSourcePrivate(client)
    , m_player(player)
{
}

MediaSourcePrivateRitterOS::~MediaSourcePrivateRitterOS() = default;

RefPtr<MediaPlayerPrivateInterface> MediaSourcePrivateRitterOS::player() const
{
    return m_player.get();
}

void MediaSourcePrivateRitterOS::setPlayer(MediaPlayerPrivateInterface* player)
{
    m_player = downcast<MediaPlayerPrivateRitterOS>(player);
}

MediaSourcePrivate::AddStatus MediaSourcePrivateRitterOS::addSourceBuffer(const ContentType& type, const MediaSourceConfiguration&, RefPtr<SourceBufferPrivate>& sourceBuffer)
{
    MediaEngineSupportParameters parameters {
        .platformType = PlatformMediaDecodingType::MediaSource,
        .type = type
    };
    if (MediaPlayerPrivateRitterOS::supportsType(parameters) == MediaPlayer::SupportsType::IsNotSupported)
        return AddStatus::NotSupported;
    {
        Locker locker { m_lock };
        m_sourceBuffers.append(SourceBufferPrivateRitterOS::create(*this, type));
        sourceBuffer = m_sourceBuffers.last();
    }
    sourceBuffer->setMediaSourceDuration(duration());
    return AddStatus::Ok;
}

void MediaSourcePrivateRitterOS::durationChanged(const MediaTime& duration)
{
    MediaSourcePrivate::durationChanged(duration);
    if (RefPtr player = m_player.get())
        player->durationFromMediaSourceChanged();
}

void MediaSourcePrivateRitterOS::markEndOfStream(EndOfStreamStatus status)
{
    MediaSourcePrivate::markEndOfStream(status);
    if (RefPtr player = m_player.get())
        player->wake();
}

void MediaSourcePrivateRitterOS::unmarkEndOfStream()
{
    MediaSourcePrivate::unmarkEndOfStream();
}

void MediaSourcePrivateRitterOS::notifyActiveSourceBuffersChanged()
{
    if (RefPtr player = m_player.get())
        player->notifyActiveSourceBuffersChanged();
}

void MediaSourcePrivateRitterOS::provideMoreSamplesIfWaiting()
{
    for (auto& sourceBuffer : sourceBuffers())
        downcast<SourceBufferPrivateRitterOS>(sourceBuffer.get()).provideMoreSamplesIfWaiting();
}

void MediaSourcePrivateRitterOS::videoSizeChanged(const FloatSize& size)
{
    if (RefPtr player = m_player.get())
        player->mediaSourceVideoSizeChanged(size);
}

} // namespace WebCore

#endif

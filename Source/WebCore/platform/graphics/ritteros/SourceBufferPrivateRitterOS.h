/*
 * RitterOS: a MediaSource's SourceBuffer for the media engine.
 *
 * The bytes the page appends are cut into segments here: for MP4 (ISO
 * BMFF) the top-level boxes (ftyp + moov: an initialization segment;
 * moof + mdat: a media segment), for WebM the EBML elements (EBML header,
 * Segment, its Info/Tracks: an initialization segment; each Cluster: a
 * media segment). FFmpeg reads each one: the initialization segment alone
 * gives the tracks, and a media segment behind its initialization segment
 * gives the samples (MediaSampleRitterOS: a compressed packet and its
 * times). A segment is read once it has all come; until then its bytes
 * wait for the next append.
 */

#pragma once

#if ENABLE(MEDIA_SOURCE) && PLATFORM(RITTEROS)

#include "SourceBufferPrivate.h"
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>

namespace WebCore {

class MediaCodecConfigRitterOS;
class MediaSourcePrivateRitterOS;

class SourceBufferPrivateRitterOS final : public SourceBufferPrivate {
public:
    static Ref<SourceBufferPrivateRitterOS> create(MediaSourcePrivateRitterOS&, const ContentType&);
    ~SourceBufferPrivateRitterOS();

    constexpr MediaPlatformType platformType() const final { return MediaPlatformType::RitterOS; }

    // The pipeline has room: enqueue what was waiting for it.
    void provideMoreSamplesIfWaiting();

private:
    SourceBufferPrivateRitterOS(MediaSourcePrivateRitterOS&, bool isWebM);

    Ref<MediaPromise> appendInternal(Ref<SharedBuffer>&&) final;
    void resetParserStateInternal() final;
    bool canSwitchToType(const ContentType&) final;
    void flush(TrackID) final;
    void enqueueSample(Ref<MediaSample>&&, TrackID) final;
    void allSamplesInTrackEnqueued(TrackID) final;
    bool isReadyForMoreSamples(TrackID) final;
    void notifyClientWhenReadyForMoreSamples(TrackID) final;
#if !RELEASE_LOG_DISABLED
    const Logger& sourceBufferLogger() const final;
    uint64_t sourceBufferLogIdentifier() final { return 0; }
#endif

    RefPtr<MediaSourcePrivateRitterOS> ritterosMediaSource() const;
    bool isTrackVideo(TrackID) const;
    bool parseSegments();
    bool parseMP4();
    bool parseWebM();
    bool readInitializationSegment();
    bool readMediaSegment(std::span<const uint8_t>);

    struct Track {
        TrackID id { 0 };
        bool isVideo { false };
        RefPtr<MediaCodecConfigRitterOS> config;
    };

    bool m_isWebM { false };
    Vector<uint8_t> m_input;          // appended, not yet a whole segment
    Vector<uint8_t> m_initialization; // the last initialization segment
    bool m_initializationRead { false };
    // WebM: whether the current initialization segment's elements are still
    // coming (they end at its first Cluster).
    bool m_inWebMHeader { false };
    // MP4: a moof waiting for its mdat (the offset of the moof in m_input).
    std::optional<size_t> m_pendingMoof;
    Vector<Track> m_tracks;           // by FFmpeg's stream index
    HashMap<TrackID, size_t> m_trackIndex;
    HashSet<TrackID> m_waitingForRoom;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::SourceBufferPrivateRitterOS)
static bool isType(const WebCore::SourceBufferPrivate& sourceBuffer) { return sourceBuffer.platformType() == WebCore::MediaPlatformType::RitterOS; }
SPECIALIZE_TYPE_TRAITS_END()

#endif

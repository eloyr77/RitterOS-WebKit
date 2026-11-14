/*
 * RitterOS: a MediaSource's SourceBuffer (SourceBufferPrivateRitterOS.h).
 */

#include "config.h"
#include "SourceBufferPrivateRitterOS.h"

#if ENABLE(MEDIA_SOURCE) && PLATFORM(RITTEROS)

#include "AudioTrackPrivate.h"
#include "ContentType.h"
#include "MediaDescription.h"
#include "MediaPipelineRitterOS.h"
#include "MediaPlayerPrivateRitterOS.h"
#include "MediaSample.h"
#include "MediaSourcePrivateRitterOS.h"
#include "SharedBuffer.h"
#include "SourceBufferPrivateClient.h"
#include "VideoTrackPrivate.h"
#include <wtf/Logger.h>
#include <wtf/NativePromise.h>
#include <wtf/TZoneMallocInlines.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace WebCore {

// --- Samples, tracks ------------------------------------------------------------

class MediaSampleDataRitterOS : public ThreadSafeRefCounted<MediaSampleDataRitterOS> {
public:
    static Ref<MediaSampleDataRitterOS> create(std::span<const uint8_t> bytes) { return adoptRef(*new MediaSampleDataRitterOS(bytes)); }
    const Vector<uint8_t>& bytes() const { return m_bytes; }
private:
    explicit MediaSampleDataRitterOS(std::span<const uint8_t> bytes)
        : m_bytes(bytes)
    {
    }
    Vector<uint8_t> m_bytes;
};

class MediaSampleRitterOS final : public MediaSample {
public:
    struct Info {
        TrackID trackID { 0 };
        bool isVideo { false };
        RefPtr<MediaCodecConfigRitterOS> config;
        RefPtr<MediaSampleDataRitterOS> data;
        MediaTime presentationTime;
        MediaTime decodeTime;
        MediaTime duration;
        FloatSize size;
        bool isSync { false };
        bool isNonDisplaying { false };
    };
    static Ref<MediaSampleRitterOS> create(Info&& info) { return adoptRef(*new MediaSampleRitterOS(WTF::move(info))); }

    const Info& info() const { return m_info; }

    MediaTime presentationTime() const final { return m_info.presentationTime; }
    MediaTime decodeTime() const final { return m_info.decodeTime; }
    MediaTime duration() const final { return m_info.duration; }
    TrackID trackID() const final { return m_info.trackID; }
    size_t sizeInBytes() const final { return m_info.data->bytes().size() + sizeof(*this); }
    FloatSize presentationSize() const final { return m_info.size; }
    void offsetTimestampsBy(const MediaTime& offset) final
    {
        m_info.presentationTime += offset;
        m_info.decodeTime += offset;
    }
    void setTimestamps(const MediaTime& presentationTime, const MediaTime& decodeTime) final
    {
        m_info.presentationTime = presentationTime;
        m_info.decodeTime = decodeTime;
    }
    Ref<MediaSample> createNonDisplayingCopy() const final
    {
        auto info = m_info;
        info.isNonDisplaying = true;
        return create(WTF::move(info));
    }
    Ref<MediaSample> createCopyWithAdjustedStartTime(const MediaTime& offset) const final
    {
        auto clamped = std::max(MediaTime::zeroTime(), std::min(offset, m_info.duration));
        auto info = m_info;
        info.presentationTime += clamped;
        info.decodeTime += clamped;
        info.duration -= clamped;
        return create(WTF::move(info));
    }
    SampleFlags flags() const final
    {
        unsigned flags = None;
        if (m_info.isSync)
            flags |= IsSync;
        if (m_info.isNonDisplaying)
            flags |= IsNonDisplaying;
        return static_cast<SampleFlags>(flags);
    }
    PlatformSample platformSample() const final { return PlatformSample { static_cast<const MockSampleBox*>(nullptr) }; }
    Type type() const final { return Type::None; }
    void dump(PrintStream& out) const final
    {
        out.print("{PTS(", presentationTime(), "), DTS(", decodeTime(), "), duration(", duration(), "), sync(", m_info.isSync, ")}");
    }

private:
    explicit MediaSampleRitterOS(Info&& info)
        : m_info(WTF::move(info))
    {
    }
    Info m_info;
};

class MediaDescriptionRitterOS final : public MediaDescription {
public:
    static Ref<MediaDescriptionRitterOS> create(String&& codec, bool isVideo) { return adoptRef(*new MediaDescriptionRitterOS(WTF::move(codec), isVideo)); }
    bool isVideo() const final { return m_isVideo; }
    bool isAudio() const final { return !m_isVideo; }
    bool isText() const final { return false; }
private:
    MediaDescriptionRitterOS(String&& codec, bool isVideo)
        : MediaDescription(WTF::move(codec))
        , m_isVideo(isVideo)
    {
    }
    const bool m_isVideo;
};

class VideoTrackPrivateRitterOS final : public VideoTrackPrivate {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(VideoTrackPrivateRitterOS);
public:
    static Ref<VideoTrackPrivateRitterOS> create(TrackID id) { return adoptRef(*new VideoTrackPrivateRitterOS(id)); }
    TrackID id() const final { return m_id; }
private:
    explicit VideoTrackPrivateRitterOS(TrackID id)
        : m_id(id)
    {
    }
    const TrackID m_id;
};

class AudioTrackPrivateRitterOS final : public AudioTrackPrivate {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(AudioTrackPrivateRitterOS);
public:
    static Ref<AudioTrackPrivateRitterOS> create(TrackID id) { return adoptRef(*new AudioTrackPrivateRitterOS(id)); }
    TrackID id() const final { return m_id; }
private:
    explicit AudioTrackPrivateRitterOS(TrackID id)
        : m_id(id)
    {
    }
    const TrackID m_id;
};

// The codec's name as MSE's tracks know it (changes of it are type changes).
static String codecString(AVCodecID codec)
{
    switch (codec) {
    case AV_CODEC_ID_H264: return "avc1"_s;
    case AV_CODEC_ID_AAC: return "mp4a"_s;
    case AV_CODEC_ID_MP3: return "mp3"_s;
    case AV_CODEC_ID_VP8: return "vp08"_s;
    case AV_CODEC_ID_VP9: return "vp09"_s;
    case AV_CODEC_ID_OPUS: return "opus"_s;
    case AV_CODEC_ID_VORBIS: return "vorbis"_s;
    case AV_CODEC_ID_FLAC: return "flac"_s;
    default: return String::fromLatin1(avcodec_get_name(codec));
    }
}

// --- Reading a segment with FFmpeg ----------------------------------------------------

namespace {

// FFmpeg reads a segment from memory.
struct MemoryReader {
    std::span<const uint8_t> bytes;
    size_t position { 0 };

    static int read(void* opaque, uint8_t* buffer, int size)
    {
        auto* reader = static_cast<MemoryReader*>(opaque);
        size_t left = reader->bytes.size() - reader->position;
        if (!left)
            return AVERROR_EOF;
        size_t count = std::min<size_t>(left, size);
        memcpy(buffer, reader->bytes.subspan(reader->position, count).data(), count);
        reader->position += count;
        return static_cast<int>(count);
    }
    static int64_t seek(void* opaque, int64_t offset, int whence)
    {
        auto* reader = static_cast<MemoryReader*>(opaque);
        switch (whence & ~AVSEEK_FORCE) {
        case AVSEEK_SIZE:
            return reader->bytes.size();
        case SEEK_SET:
            break;
        case SEEK_CUR:
            offset += reader->position;
            break;
        case SEEK_END:
            offset += reader->bytes.size();
            break;
        default:
            return -1;
        }
        if (offset < 0 || static_cast<uint64_t>(offset) > reader->bytes.size())
            return -1;
        reader->position = offset;
        return offset;
    }
};

class Demuxer {
public:
    Demuxer(std::span<const uint8_t> bytes, bool isWebM)
        : m_reader { bytes }
    {
        constexpr int bufferSize = 32 * 1024;
        m_io = avio_alloc_context(static_cast<uint8_t*>(av_malloc(bufferSize)), bufferSize, 0, &m_reader, MemoryReader::read, nullptr, MemoryReader::seek);
        m_format = avformat_alloc_context();
        m_format->pb = m_io;
        m_format->flags |= AVFMT_FLAG_CUSTOM_IO;
        AVDictionary* options = nullptr;
        if (!isWebM) {
            // Fragmented MP4: the times are the fragments' (tfdt), as MSE has them.
            av_dict_set(&options, "ignore_editlist", "1", 0);
        }
        const AVInputFormat* inputFormat = av_find_input_format(isWebM ? "matroska" : "mov");
        if (avformat_open_input(&m_format, nullptr, inputFormat, &options) < 0)
            m_format = nullptr; // (freed by avformat_open_input)
        av_dict_free(&options);
    }
    ~Demuxer()
    {
        if (m_format)
            avformat_close_input(&m_format);
        if (m_io) {
            av_freep(&m_io->buffer);
            avio_context_free(&m_io);
        }
    }
    AVFormatContext* format() const { return m_format; }

private:
    MemoryReader m_reader;
    AVIOContext* m_io { nullptr };
    AVFormatContext* m_format { nullptr };
};

MediaTime exactTime(int64_t value, AVRational timeBase)
{
    if (value == AV_NOPTS_VALUE)
        return MediaTime::invalidTime();
    if (timeBase.num == 1 && timeBase.den > 0)
        return MediaTime(value, timeBase.den);
    return MediaTime(av_rescale_q(value, timeBase, AVRational { 1, 1000000 }), 1000000);
}

// Whether a WebM header has its Tracks element (the ID's bytes in it).
bool containsTracks(std::span<const uint8_t> bytes)
{
    for (size_t i = 0; i + 4 <= bytes.size(); i++) {
        if (bytes[i] == 0x16 && bytes[i + 1] == 0x54 && bytes[i + 2] == 0xAE && bytes[i + 3] == 0x6B)
            return true;
    }
    return false;
}

uint32_t readBigEndian32(std::span<const uint8_t> bytes, size_t at)
{
    return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) | (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
}

} // namespace

// --- SourceBufferPrivateRitterOS ----------------------------------------------------------

Ref<SourceBufferPrivateRitterOS> SourceBufferPrivateRitterOS::create(MediaSourcePrivateRitterOS& parent, const ContentType& type)
{
    bool isWebM = type.containerType().containsIgnoringASCIICase("webm"_s);
    return adoptRef(*new SourceBufferPrivateRitterOS(parent, isWebM));
}

SourceBufferPrivateRitterOS::SourceBufferPrivateRitterOS(MediaSourcePrivateRitterOS& parent, bool isWebM)
    : SourceBufferPrivate(parent)
    , m_isWebM(isWebM)
{
}

SourceBufferPrivateRitterOS::~SourceBufferPrivateRitterOS() = default;

RefPtr<MediaSourcePrivateRitterOS> SourceBufferPrivateRitterOS::ritterosMediaSource() const
{
    return dynamicDowncast<MediaSourcePrivateRitterOS>(m_mediaSource.get());
}

#if !RELEASE_LOG_DISABLED
const Logger& SourceBufferPrivateRitterOS::sourceBufferLogger() const
{
    static NeverDestroyed<Ref<Logger>> logger = Logger::create(this);
    return logger.get();
}
#endif

Ref<MediaPromise> SourceBufferPrivateRitterOS::appendInternal(Ref<SharedBuffer>&& data)
{
    m_input.append(data->span());
    if (!parseSegments()) {
        m_input.clear();
        m_pendingMoof.reset();
        return MediaPromise::createAndReject(PlatformMediaError::ParsingError);
    }
    return MediaPromise::createAndResolve();
}

void SourceBufferPrivateRitterOS::resetParserStateInternal()
{
    // (A partial segment is dropped; the initialization segment stays.)
    m_input.clear();
    m_pendingMoof.reset();
}

bool SourceBufferPrivateRitterOS::parseSegments()
{
    return m_isWebM ? parseWebM() : parseMP4();
}

bool SourceBufferPrivateRitterOS::parseMP4()
{
    std::span<const uint8_t> input = m_input.span();
    size_t position = 0;
    size_t consumed = 0;
    while (input.size() - position >= 8) {
        uint64_t size = readBigEndian32(input, position);
        uint32_t type = readBigEndian32(input, position + 4);
        size_t header = 8;
        if (size == 1) {
            if (input.size() - position < 16)
                break;
            size = (uint64_t(readBigEndian32(input, position + 8)) << 32) | readBigEndian32(input, position + 12);
            header = 16;
        } else if (!size)
            break; // (to the end of the stream: never whole)
        if (size < header)
            return false;
        if (input.size() - position < size)
            break;
        auto box = input.subspan(position, size);
        switch (type) {
        case 0x66747970: // ftyp: an initialization segment begins
            m_initialization = Vector<uint8_t>(box);
            m_initializationRead = false;
            m_pendingMoof.reset();
            break;
        case 0x6d6f6f76: // moov
            if (!m_initialization.isEmpty() && readBigEndian32(m_initialization.span(), 4) == 0x66747970)
                m_initialization.shrink(readBigEndian32(m_initialization.span(), 0)); // keep its ftyp only
            m_initialization.append(box);
            if (!readInitializationSegment())
                return false;
            break;
        case 0x6d6f6f66: // moof: its mdat follows
            m_pendingMoof = position;
            break;
        case 0x6d646174: // mdat
            if (m_pendingMoof) {
                if (!readMediaSegment(input.subspan(*m_pendingMoof, position + size - *m_pendingMoof)))
                    return false;
                m_pendingMoof.reset();
            }
            break;
        default: // styp, sidx, emsg, prft, free: nothing to play
            break;
        }
        position += size;
        if (!m_pendingMoof)
            consumed = position;
    }
    if (consumed) {
        m_input.removeAt(0, consumed);
        if (m_pendingMoof)
            *m_pendingMoof -= consumed;
    }
    return true;
}

bool SourceBufferPrivateRitterOS::parseWebM()
{
    // EBML: an element is its ID (1-4 bytes, the length in the leading
    // zeros of the first), its size (1-8 bytes, likewise; all ones: not
    // known), then its data.
    std::span<const uint8_t> input = m_input.span();
    auto readNumber = [&](size_t at, size_t maxLength, bool keepMarker, uint64_t& value, size_t& length, bool& unknown) -> bool {
        if (at >= input.size())
            return false;
        uint8_t first = input[at];
        length = 1;
        while (length <= maxLength && !(first & (0x80 >> (length - 1))))
            length++;
        if (length > maxLength || at + length > input.size())
            return false;
        value = keepMarker ? first : (first & (0xff >> length));
        bool allOnes = value == (keepMarker ? first : static_cast<uint64_t>(0xff >> length));
        for (size_t i = 1; i < length; i++) {
            value = (value << 8) | input[at + i];
            allOnes = allOnes && input[at + i] == 0xff;
        }
        unknown = allOnes;
        return true;
    };

    constexpr uint64_t ebmlHeader = 0x1A45DFA3, segment = 0x18538067, cluster = 0x1F43B675;
    constexpr uint64_t info = 0x1549A966, tracks = 0x1654AE6B;
    size_t position = 0;
    size_t consumed = 0;
    while (true) {
        uint64_t id, size;
        size_t idLength, sizeLength;
        bool unknownId, unknownSize;
        if (!readNumber(position, 4, true, id, idLength, unknownId))
            break;
        if (!readNumber(position + idLength, 8, false, size, sizeLength, unknownSize))
            break;
        size_t header = idLength + sizeLength;
        if (id == segment) {
            // Its children follow at the top level here; its size becomes
            // "not known" (the demuxer is given parts of it).
            static constexpr uint8_t unknownSegment[] = { 0x18, 0x53, 0x80, 0x67, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
            m_initialization.append(std::span { unknownSegment });
            position += header;
            consumed = position;
            continue;
        }
        if (unknownSize)
            return false; // (a live stream's Cluster of no known size: not yet)
        if (input.size() - position - header < size)
            break;
        auto element = input.subspan(position, header + size);
        if (id == ebmlHeader) {
            m_initialization = Vector<uint8_t>(element);
            m_initializationRead = false;
            m_inWebMHeader = true;
        } else if (id == cluster) {
            if (m_inWebMHeader && !m_initializationRead && !readInitializationSegment())
                return false;
            m_inWebMHeader = false;
            if (!readMediaSegment(element))
                return false;
        } else if (m_inWebMHeader && (id == info || id == tracks))
            m_initialization.append(element);
        position += header + size;
        consumed = position;
    }
    if (consumed)
        m_input.removeAt(0, consumed);
    // An initialization segment appended alone: its tracks now.
    if (m_inWebMHeader && !m_initializationRead && m_input.isEmpty()) {
        // (Its Tracks may come in a later append: read it once they are there.)
        if (m_initialization.span().size() > 4 && containsTracks(m_initialization.span()) && !readInitializationSegment())
            return false;
    }
    return true;
}

bool SourceBufferPrivateRitterOS::readInitializationSegment()
{
    // FFmpeg's Matroska demuxer gives no streams for a header that ends
    // where it ends: an empty Cluster after it (its Timestamp only) does.
    Vector<uint8_t> bytes(m_initialization.span());
    if (m_isWebM) {
        static constexpr uint8_t emptyCluster[] = { 0x1F, 0x43, 0xB6, 0x75, 0x83, 0xE7, 0x81, 0x00 };
        bytes.append(std::span { emptyCluster });
    }
    Demuxer demuxer(bytes.span(), m_isWebM);
    auto* format = demuxer.format();
    if (!format)
        return false;
    SourceBufferPrivateClient::InitializationSegment segment;
    segment.duration = format->duration > 0 ? MediaTime(format->duration, AV_TIME_BASE) : MediaTime::invalidTime();
    m_tracks.clear();
    m_trackIndex.clear();
    m_tracks.resize(format->nb_streams);
    RefPtr mediaSource = ritterosMediaSource();
    for (unsigned i = 0; i < format->nb_streams; i++) {
        auto* stream = format->streams[i];
        auto* parameters = stream->codecpar;
        bool isVideo = parameters->codec_type == AVMEDIA_TYPE_VIDEO;
        if (!isVideo && parameters->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        TrackID id = stream->id > 0 ? stream->id : i + 1;
        auto config = MediaCodecConfigRitterOS::create(parameters, stream->time_base.num, stream->time_base.den);
        m_tracks[i] = { id, isVideo, config.copyRef() };
        m_trackIndex.set(id, i);
        if (isVideo) {
            segment.videoTracks.append({ MediaDescriptionRitterOS::create(codecString(parameters->codec_id), true), VideoTrackPrivateRitterOS::create(id) });
            if (mediaSource)
                mediaSource->videoSizeChanged(FloatSize(config->width(), config->height()));
        } else
            segment.audioTracks.append({ MediaDescriptionRitterOS::create(codecString(parameters->codec_id), false), AudioTrackPrivateRitterOS::create(id) });
    }
    m_initializationRead = true;
    didReceiveInitializationSegment(WTF::move(segment));
    return true;
}

bool SourceBufferPrivateRitterOS::readMediaSegment(std::span<const uint8_t> bytes)
{
    if (m_initialization.isEmpty())
        return false;
    if (!m_initializationRead && !readInitializationSegment())
        return false;
    Vector<uint8_t> whole;
    whole.reserveInitialCapacity(m_initialization.size() + bytes.size());
    whole.append(m_initialization.span());
    whole.append(bytes);
    Demuxer demuxer(whole.span(), m_isWebM);
    auto* format = demuxer.format();
    if (!format)
        return false;

    // Read them all first: a WebM block has no duration, which is the time
    // to the next one (in presentation order) of its track.
    Vector<MediaSampleRitterOS::Info> samples;
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(format, packet) >= 0) {
        unsigned index = packet->stream_index;
        if (index < m_tracks.size() && m_tracks[index].config) {
            auto& track = m_tracks[index];
            auto timeBase = format->streams[index]->time_base;
            MediaSampleRitterOS::Info info;
            info.trackID = track.id;
            info.isVideo = track.isVideo;
            info.config = track.config;
            info.data = MediaSampleDataRitterOS::create(std::span<const uint8_t>(packet->data, packet->size));
            info.presentationTime = exactTime(packet->pts, timeBase);
            info.decodeTime = exactTime(packet->dts, timeBase);
            if (!info.decodeTime.isValid())
                info.decodeTime = info.presentationTime;
            info.duration = packet->duration > 0 ? exactTime(packet->duration, timeBase) : MediaTime::invalidTime();
            info.isSync = packet->flags & AV_PKT_FLAG_KEY;
            if (track.isVideo)
                info.size = FloatSize(track.config->width(), track.config->height());
            if (info.presentationTime.isValid())
                samples.append(WTF::move(info));
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);

    for (auto& track : m_tracks) {
        if (!track.config)
            continue;
        Vector<size_t> order;
        for (size_t i = 0; i < samples.size(); i++) {
            if (samples[i].trackID == track.id)
                order.append(i);
        }
        std::ranges::sort(order, [&](size_t a, size_t b) { return samples[a].presentationTime < samples[b].presentationTime; });
        MediaTime last = track.isVideo ? MediaTime(1, 30) : MediaTime(20, 1000);
        for (size_t k = 0; k < order.size(); k++) {
            auto& info = samples[order[k]];
            if (info.duration.isValid() && info.duration > MediaTime::zeroTime()) {
                last = info.duration;
                continue;
            }
            if (k + 1 < order.size() && samples[order[k + 1]].presentationTime > info.presentationTime)
                info.duration = samples[order[k + 1]].presentationTime - info.presentationTime;
            else
                info.duration = last;
            last = info.duration;
        }
    }
    for (auto& info : samples)
        didReceiveSample(MediaSampleRitterOS::create(WTF::move(info)));
    return true;
}

bool SourceBufferPrivateRitterOS::canSwitchToType(const ContentType& type)
{
    MediaEngineSupportParameters parameters {
        .platformType = PlatformMediaDecodingType::MediaSource,
        .type = type
    };
    if (MediaPlayerPrivateRitterOS::supportsType(parameters) == MediaPlayer::SupportsType::IsNotSupported)
        return false;
    // (The cutting into segments is the container's: another container is
    // another SourceBuffer.)
    return type.containerType().containsIgnoringASCIICase("webm"_s) == m_isWebM;
}

static MediaPipelineRitterOS* pipelineOf(const RefPtr<MediaSourcePrivateRitterOS>& mediaSource)
{
    if (!mediaSource)
        return nullptr;
    RefPtr player = mediaSource->ritterosPlayer();
    return player ? &player->pipeline() : nullptr;
}

bool SourceBufferPrivateRitterOS::isTrackVideo(TrackID trackID) const
{
    auto index = m_trackIndex.find(trackID);
    return index != m_trackIndex.end() && m_tracks[index->value].isVideo;
}

void SourceBufferPrivateRitterOS::flush(TrackID trackID)
{
    m_waitingForRoom.remove(trackID);
    if (auto* pipeline = pipelineOf(ritterosMediaSource())) {
        if (isTrackVideo(trackID))
            pipeline->flushVideo();
        else
            pipeline->flushAudio();
    }
}

void SourceBufferPrivateRitterOS::enqueueSample(Ref<MediaSample>&& sample, TrackID trackID)
{
    auto* pipeline = pipelineOf(ritterosMediaSource());
    if (!pipeline || sample->type() != MediaSample::Type::None)
        return;
    auto& info = static_cast<MediaSampleRitterOS&>(sample.get()).info();
    MediaPacketRitterOS packet;
    packet.config = info.config;
    packet.data = info.data->bytes();
    packet.presentationTime = info.presentationTime;
    packet.decodeTime = info.decodeTime;
    packet.duration = info.duration;
    packet.isKeyFrame = info.isSync;
    packet.isNonDisplaying = info.isNonDisplaying;
    bool pushed = isTrackVideo(trackID) ? pipeline->pushVideo(WTF::move(packet)) : pipeline->pushAudio(WTF::move(packet));
    UNUSED_VARIABLE(pushed); // (isReadyForMoreSamples came first)
}

void SourceBufferPrivateRitterOS::allSamplesInTrackEnqueued(TrackID trackID)
{
    if (auto* pipeline = pipelineOf(ritterosMediaSource())) {
        if (isTrackVideo(trackID))
            pipeline->endOfVideo();
        else
            pipeline->endOfAudio();
    }
}

bool SourceBufferPrivateRitterOS::isReadyForMoreSamples(TrackID trackID)
{
    auto* pipeline = pipelineOf(ritterosMediaSource());
    if (!pipeline)
        return false;
    return isTrackVideo(trackID) ? pipeline->canAcceptVideo() : pipeline->canAcceptAudio();
}

void SourceBufferPrivateRitterOS::notifyClientWhenReadyForMoreSamples(TrackID trackID)
{
    m_waitingForRoom.add(trackID);
}

void SourceBufferPrivateRitterOS::provideMoreSamplesIfWaiting()
{
    if (m_waitingForRoom.isEmpty())
        return;
    for (auto trackID : copyToVector(m_waitingForRoom)) {
        if (!isReadyForMoreSamples(trackID))
            continue;
        m_waitingForRoom.remove(trackID);
        provideMediaData(trackID);
    }
}

} // namespace WebCore

#endif

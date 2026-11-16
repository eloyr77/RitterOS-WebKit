/*
 * RitterOS: the playing half of the media engine (MediaPlayerPrivateRitterOS):
 * compressed packets in (from a file's demuxer or from Media Source
 * Extensions' SourceBuffers), pictures and sound out.
 *
 *  - a video thread decodes (FFmpeg) and converts each picture to BGRA
 *    (swscale) into a NativeImage, a few pictures ahead, at the size the
 *    page shows it at in the screen's pixels (setDisplaySize: the page's
 *    paint then copies it as it is; stretched there, by Skia on the main
 *    thread, a full-screen 720p video took 15 ms a picture on the Acer);
 *  - an audio thread decodes, resamples to the sound device's format
 *    (48 kHz, 16-bit, stereo) and writes it, paced to the clock;
 *  - the clock is the computer's monotonic clock (playing: the time of the
 *    last start plus the time since), held while the decoders have nothing
 *    to give (stalled); the sound is written to be heard at its time (the
 *    system says how much is buffered: AudioOutputRitterOS), and kept so
 *    by stretching it a little where the device's crystal and the clock
 *    drift apart (the resampler's compensation).
 *
 * The player's main thread takes the picture due (currentImage) and asks
 * the page to repaint when it changes. Packets carry their decoder's setup
 * (MediaCodecConfigRitterOS): a new one (another resolution of a stream)
 * reopens the decoder.
 */

#pragma once

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include "IntSize.h"
#include "NativeImage.h"
#include <memory>
#include <wtf/Condition.h>
#include <wtf/Deque.h>
#include <wtf/Lock.h>
#include <wtf/MediaTime.h>
#include <wtf/MonotonicTime.h>
#include <wtf/Threading.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Vector.h>

struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace WebCore {

class AudioOutputRitterOS;

// A stream's decoder setup (FFmpeg's codec parameters, copied) and the
// time base its packets' times are in.
class MediaCodecConfigRitterOS : public ThreadSafeRefCounted<MediaCodecConfigRitterOS> {
public:
    static Ref<MediaCodecConfigRitterOS> create(const AVCodecParameters*, int timeBaseNumerator, int timeBaseDenominator);
    ~MediaCodecConfigRitterOS();

    const AVCodecParameters* parameters() const { return m_parameters; }
    bool isVideo() const;
    bool isAudio() const;
    int width() const;
    int height() const;
    String codecName() const;
    // Whether a decoder set up from other can take this one's packets.
    bool sameDecoderAs(const MediaCodecConfigRitterOS& other) const;

private:
    MediaCodecConfigRitterOS() = default;
    AVCodecParameters* m_parameters { nullptr };
};

// One compressed packet and its times (already converted from the stream's
// time base).
struct MediaPacketRitterOS {
    RefPtr<MediaCodecConfigRitterOS> config;
    Vector<uint8_t> data;
    MediaTime presentationTime;
    MediaTime decodeTime;
    MediaTime duration;
    bool isKeyFrame { false };
    bool isNonDisplaying { false }; // decoded (others need it), not shown
};

class MediaPipelineRitterOS : public ThreadSafeRefCounted<MediaPipelineRitterOS> {
public:
    static Ref<MediaPipelineRitterOS> create();
    ~MediaPipelineRitterOS();

    // Called once from the player's destruction: ends the threads.
    void stop();

    // What the threads call when the main thread has something to do (a
    // picture came, room for packets, the end): the player's wake().
    void setWakeMainThread(Function<void()>&&);
    void wakeMainThread();

    // The streams there are (before packets come; a change of codec within a
    // stream comes with the packets).
    void setHasVideo(bool);
    void setHasAudio(bool);

    // From any thread. False (nothing taken) when the queue is full: try
    // again after wakeMainThread or canAccept says so.
    bool pushVideo(MediaPacketRitterOS&&);
    bool pushAudio(MediaPacketRitterOS&&);
    bool canAcceptVideo() const;
    bool canAcceptAudio() const;
    void endOfVideo();
    void endOfAudio();

    // Drop everything queued and decoded (a seek): the clock goes to time,
    // pictures before it are decoded but not shown.
    void flush(const MediaTime&);
    void flushVideo();
    void flushAudio();

    void setPlaying(bool);
    bool isPlaying() const;
    void setVolume(float);
    void setMuted(bool);

    MediaTime currentTime() const;
    // True when playing has to wait for data (the clock is held).
    bool isStalled() const;
    // Nothing more to play: the ends were reached and everything played.
    bool isEnded() const;

    // On the main thread: advance to the picture due now. True when the
    // picture to show changed (the page repaints).
    bool updateCurrentImage();
    // How long until the next decoded picture is due (none: nothing queued,
    // or the clock is not running).
    std::optional<Seconds> timeUntilNextPicture() const;
    // COMPASS_MEDIA_STATS: pictures shown and skipped, how late they were
    // shown, since the last call (a line to standard error).
    void reportStatistics();
    RefPtr<NativeImage> currentImage() const;
    // On the main thread (the page's paint): the size the picture covers on
    // the screen, in its pixels; the pictures converted from then on have
    // it (empty: the video's own size).
    void setDisplaySize(IntSize);
    // The current picture at the video's own size (canvas's drawImage,
    // which takes the video's size for its source), converted again from
    // its decoded frame when it was made at another size.
    RefPtr<NativeImage> currentNaturalImage();
    bool hasFirstImage() const;
    MediaTime lastQueuedVideoTime() const;

    unsigned decodedFrameCount() const;
    unsigned droppedFrameCount() const;

private:
    MediaPipelineRitterOS() = default;

    struct Picture {
        MediaTime time;
        RefPtr<NativeImage> image;
        std::shared_ptr<AVFrame> frame; // (decoded, kept for currentNaturalImage)
    };

    void videoThreadMain();
    void audioThreadMain();
    MediaTime clockLocked() const;
    void holdClockLocked();
    void updateStallLocked();
    void updateRefreshUserLocked();
    // The refresh a picture handed on now reaches (where the refreshes are
    // known): the clock then, and how far past it a picture's time may be
    // and still be shown there (half a refresh, the nearest picture; moved
    // up to presentHysteresis toward the side the last one was on, so that
    // pictures due about halfway between two refreshes do not flip between
    // them).
    struct PresentTarget {
        MonotonicTime refresh;
        MediaTime clock;
        Seconds window;
        Seconds period;
    };
    std::optional<PresentTarget> presentTargetLocked(MonotonicTime now) const;

    Lock m_wakeLock;
    Function<void()> m_wakeMainThread WTF_GUARDED_BY_LOCK(m_wakeLock);
    mutable Lock m_lock;
    Condition m_condition;
    RefPtr<Thread> m_videoThread;
    RefPtr<Thread> m_audioThread;
    bool m_stopping WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_refreshUser WTF_GUARDED_BY_LOCK(m_lock) { false }; // (DisplayRefreshRitterOS)

    bool m_hasVideo WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_hasAudio WTF_GUARDED_BY_LOCK(m_lock) { false };
    Deque<MediaPacketRitterOS> m_videoPackets WTF_GUARDED_BY_LOCK(m_lock);
    Deque<MediaPacketRitterOS> m_audioPackets WTF_GUARDED_BY_LOCK(m_lock);
    size_t m_videoPacketBytes WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    size_t m_audioPacketBytes WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    bool m_videoInputEnded WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_audioInputEnded WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_videoDecodingEnded WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_audioPlayingEnded WTF_GUARDED_BY_LOCK(m_lock) { false };
    // A flush counts up: a thread's work from before it is thrown away
    // (flush(): both; MSE's flush of one track: its own).
    uint64_t m_videoGeneration WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    uint64_t m_audioGeneration WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    MediaTime m_flushTime WTF_GUARDED_BY_LOCK(m_lock);

    Deque<Picture> m_pictures WTF_GUARDED_BY_LOCK(m_lock);
    Picture m_current WTF_GUARDED_BY_LOCK(m_lock);
    IntSize m_displaySize WTF_GUARDED_BY_LOCK(m_lock);
    // currentNaturalImage's: the last one made and its converter.
    Lock m_naturalLock;
    std::shared_ptr<AVFrame> m_naturalFrame WTF_GUARDED_BY_LOCK(m_naturalLock);
    RefPtr<NativeImage> m_naturalImage WTF_GUARDED_BY_LOCK(m_naturalLock);
    SwsContext* m_naturalScaler WTF_GUARDED_BY_LOCK(m_naturalLock) { nullptr };
    bool m_hasFirstImage WTF_GUARDED_BY_LOCK(m_lock) { false };
    MediaTime m_lastQueuedVideoTime WTF_GUARDED_BY_LOCK(m_lock);
    bool m_audioStarved WTF_GUARDED_BY_LOCK(m_lock) { true };
    // The last picture's time less the clock at the refresh it was for.
    std::optional<Seconds> m_lastPresentOffset WTF_GUARDED_BY_LOCK(m_lock);

    // The clock: m_clockBase at m_clockStart when playing (and not stalled).
    bool m_playing WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_stalled WTF_GUARDED_BY_LOCK(m_lock) { false };
    MediaTime m_clockBase WTF_GUARDED_BY_LOCK(m_lock) { MediaTime::zeroTime() };
    MonotonicTime m_clockStart WTF_GUARDED_BY_LOCK(m_lock);

    float m_volume WTF_GUARDED_BY_LOCK(m_lock) { 1 };
    bool m_muted WTF_GUARDED_BY_LOCK(m_lock) { false };
    unsigned m_decodedFrames WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    unsigned m_statsShown WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    unsigned m_statsSkipped WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    double m_statsLateSum WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    double m_statsLateMax WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    unsigned m_droppedFrames WTF_GUARDED_BY_LOCK(m_lock) { 0 };
};

} // namespace WebCore

#endif

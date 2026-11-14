/*
 * RitterOS: sound out for the media engine (AudioOutputRitterOS.h).
 */

#include "config.h"
#include "AudioOutputRitterOS.h"

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/soundcard.h>
#include <sys/un.h>
#include <unistd.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(AudioOutputRitterOS);

static constexpr size_t frameBytes = AudioOutputRitterOS::channels * sizeof(int16_t);
// The device's ring (drivers/hda.c: 64 KiB) and, through soundd, its mix
// ring (16384 frames) before it: what an estimate allows at most.
static constexpr double deviceSeconds = 16384.0 / AudioOutputRitterOS::sampleRate;
// The frames kept for a pause to give back (more than is ever buffered).
static constexpr size_t historyFrames = 32768;

// soundd's messages after "RSND" (man soundd): a header, then its bytes.
enum : uint32_t { mixerSound = 1, mixerDelay = 2, mixerReset = 3 };

static bool writeAll(int fd, const void* data, size_t size)
{
    auto* bytes = static_cast<const uint8_t*>(data);
    while (size) {
        ssize_t written = ::write(fd, bytes, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        bytes += written;
        size -= written;
    }
    return true;
}

// Reads size bytes, waiting at most timeoutMilliseconds for each part.
static bool readAll(int fd, void* data, size_t size, int timeoutMilliseconds)
{
    auto* bytes = static_cast<uint8_t*>(data);
    while (size) {
        struct pollfd ready { fd, POLLIN, 0 };
        int result = poll(&ready, 1, timeoutMilliseconds);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return false;
        ssize_t got = ::read(fd, bytes, size);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            return false;
        bytes += got;
        size -= got;
    }
    return true;
}

static bool sendMessage(int fd, uint32_t type, const void* data, uint32_t size)
{
    uint32_t header[2] = { type, size };
    return writeAll(fd, header, sizeof(header)) && (!size || writeAll(fd, data, size));
}

AudioOutputRitterOS::AudioOutputRitterOS()
{
    m_history.grow(historyFrames * channels);
}

AudioOutputRitterOS::~AudioOutputRitterOS()
{
    close();
}

bool AudioOutputRitterOS::openMixer()
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    struct sockaddr_un address { };
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, "/run/sound", sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address))) {
        ::close(fd);
        return false;
    }
    // Messages (68w): it answers a delay. (An older soundd takes the bytes
    // for 3 frames of sound and says nothing; one that refuses this
    // program, 68z, closes the connection.)
    uint32_t delay;
    if (writeAll(fd, "RSND", 4) && sendMessage(fd, mixerDelay, nullptr, 0)
        && readAll(fd, &delay, sizeof(delay), 300)) {
        m_fd = fd;
        m_viaMixer = true;
        m_exact = true;
        return true;
    }
    ::close(fd);
    return false;
}

bool AudioOutputRitterOS::openDevice()
{
    m_fd = ::open("/dev/pcm0", O_WRONLY | O_CLOEXEC);
    if (m_fd < 0)
        return false;
    int bytes = 0;
    m_viaMixer = false;
    m_exact = !ioctl(m_fd, SNDCTL_DSP_GETODELAY, &bytes);
    return true;
}

bool AudioOutputRitterOS::open()
{
    if (m_fd >= 0)
        return true;
    if (m_failed)
        return false;
    // soundd plays only for root and the user at the console (OS step
    // 68z), as /dev/pcm0 allows only them: another user's connection is
    // closed at once (the delay asked reads its end), and the device says
    // why.
    if (!openMixer() && !openDevice()) {
        // (Said once: the pictures play on without sound.)
        static bool said;
        if (!said) {
            said = true;
            dprintf(2, "media: no sound (soundd refused or not running; /dev/pcm0: %s)\n", strerror(errno));
        }
        m_failed = true;
        return false;
    }
    static bool told;
    if (!told) {
        told = true;
        dprintf(2, "media: sound through %s%s\n", m_viaMixer ? "soundd" : "/dev/pcm0", m_exact ? "" : " (its delay estimated)");
    }
    m_emptyAt = MonotonicTime::now();
    return true;
}

void AudioOutputRitterOS::close()
{
    m_failed = false; // (try again at the next start)
    if (m_fd < 0)
        return;
    // (Without a reset, the device's close waits until everything written
    // was heard, and soundd plays a closed connection's sound out.)
    reset();
    ::close(m_fd);
    m_fd = -1;
}

void AudioOutputRitterOS::keep(const int16_t* frames, size_t count)
{
    if (count > historyFrames) {
        frames += (count - historyFrames) * channels;
        m_historyEnd += count - historyFrames;
        count = historyFrames;
    }
    while (count) {
        size_t at = m_historyEnd % historyFrames;
        size_t piece = std::min(count, historyFrames - at);
        memcpy(m_history.mutableSpan().data() + at * channels, frames, piece * frameBytes);
        frames += piece * channels;
        count -= piece;
        m_historyEnd += piece;
    }
}

void AudioOutputRitterOS::write(const int16_t* frames, size_t count)
{
    auto now = MonotonicTime::now();
    m_emptyAt = std::max(m_emptyAt, now) + Seconds(static_cast<double>(count) / sampleRate);
    if (m_fd < 0)
        return;
    keep(frames, count);
    size_t size = count * frameBytes;
    bool written = m_viaMixer && m_exact
        ? sendMessage(m_fd, mixerSound, frames, static_cast<uint32_t>(size))
        : writeAll(m_fd, frames, size);
    if (!written) {
        ::close(m_fd);
        m_fd = -1;
        return;
    }
    if (!m_exact) {
        // A write that waited for room: the device holds at most its ring.
        double ring = deviceSeconds * (m_viaMixer ? 2 : 1);
        auto latest = MonotonicTime::now() + Seconds(ring);
        if (m_emptyAt > latest)
            m_emptyAt = latest;
    }
}

size_t AudioOutputRitterOS::delayFrames()
{
    auto known = [&](size_t frames) {
        m_emptyAt = MonotonicTime::now() + Seconds(static_cast<double>(frames) / sampleRate);
        return frames;
    };
    if (m_fd < 0 || !m_exact)
        return static_cast<size_t>(std::max(0_s, m_emptyAt - MonotonicTime::now()).seconds() * sampleRate);
    if (m_viaMixer) {
        uint32_t bytes;
        if (sendMessage(m_fd, mixerDelay, nullptr, 0) && readAll(m_fd, &bytes, sizeof(bytes), 1000))
            return known(bytes / frameBytes);
    } else {
        int bytes = 0;
        if (!ioctl(m_fd, SNDCTL_DSP_GETODELAY, &bytes))
            return known(std::max(bytes, 0) / frameBytes);
    }
    // (Gone: soundd stopped, the device was taken away.)
    ::close(m_fd);
    m_fd = -1;
    return 0;
}

Seconds AudioOutputRitterOS::buffered()
{
    return Seconds(static_cast<double>(delayFrames()) / sampleRate);
}

void AudioOutputRitterOS::reset()
{
    m_emptyAt = MonotonicTime::now();
    if (m_fd < 0 || !m_exact)
        return;
    if (m_viaMixer)
        sendMessage(m_fd, mixerReset, nullptr, 0);
    else
        ioctl(m_fd, SNDCTL_DSP_RESET, 0);
}

Vector<int16_t> AudioOutputRitterOS::takeUnheard()
{
    if (m_fd < 0 || !m_exact)
        return { };
    // What is still to be heard after the reset (soundd's device buffer,
    // which has the mix already) was not dropped.
    size_t before = delayFrames();
    reset();
    size_t after = delayFrames();
    size_t count = std::min({ before > after ? before - after : 0, historyFrames, m_historyEnd });
    Vector<int16_t> frames(count * channels);
    for (size_t i = 0; i < count; i++) {
        size_t at = (m_historyEnd - count + i) % historyFrames;
        frames[i * 2] = m_history[at * 2];
        frames[i * 2 + 1] = m_history[at * 2 + 1];
    }
    return frames;
}

} // namespace WebCore

#endif

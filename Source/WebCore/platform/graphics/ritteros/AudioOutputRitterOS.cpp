/*
 * RitterOS: sound out for the media engine (AudioOutputRitterOS.h).
 */

#include "config.h"
#include "AudioOutputRitterOS.h"

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(AudioOutputRitterOS);

// The device's ring (drivers/hda.c: 64 KiB) and, through soundd, its mix
// ring (16384 frames) before it.
static constexpr double deviceSeconds = 16384.0 / AudioOutputRitterOS::sampleRate;

AudioOutputRitterOS::~AudioOutputRitterOS()
{
    close();
}

bool AudioOutputRitterOS::open()
{
    if (m_fd >= 0)
        return true;
    if (m_failed)
        return false;
    m_fd = ::open("/dev/pcm0", O_WRONLY | O_CLOEXEC);
    m_viaMixer = false;
    if (m_fd < 0) {
        // Taken (soundd has it, or another player): the mixer, if it runs.
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0) {
            struct sockaddr_un address { };
            address.sun_family = AF_UNIX;
            strncpy(address.sun_path, "/run/sound", sizeof(address.sun_path) - 1);
            if (!connect(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address))) {
                m_fd = fd;
                m_viaMixer = true;
            } else
                ::close(fd);
        }
    }
    if (m_fd < 0) {
        // (Said once: the pictures play on without sound.)
        static bool said;
        if (!said) {
            said = true;
            dprintf(2, "media: no sound (/dev/pcm0: %s, and no soundd)\n", strerror(errno));
        }
        m_failed = true;
        return false;
    }
    m_emptyAt = MonotonicTime::now();
    return true;
}

void AudioOutputRitterOS::close()
{
    if (m_fd < 0) {
        m_failed = false; // (try again at the next start)
        return;
    }
    // close() of /dev/pcmN waits until everything written was heard.
    ::close(m_fd);
    m_fd = -1;
    m_failed = false;
}

void AudioOutputRitterOS::write(const int16_t* frames, size_t count)
{
    auto now = MonotonicTime::now();
    m_emptyAt = std::max(m_emptyAt, now) + Seconds(static_cast<double>(count) / sampleRate);
    if (m_fd < 0)
        return;
    auto* bytes = reinterpret_cast<const uint8_t*>(frames);
    size_t left = count * channels * sizeof(int16_t);
    while (left) {
        ssize_t written = ::write(m_fd, bytes, left);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            ::close(m_fd);
            m_fd = -1;
            return;
        }
        bytes += written;
        left -= written;
    }
    // A write that waited for room: the device holds at most its ring.
    double ring = deviceSeconds * (m_viaMixer ? 2 : 1);
    auto latest = MonotonicTime::now() + Seconds(ring);
    if (m_emptyAt > latest)
        m_emptyAt = latest;
}

Seconds AudioOutputRitterOS::buffered() const
{
    return std::max(0_s, m_emptyAt - MonotonicTime::now());
}

} // namespace WebCore

#endif

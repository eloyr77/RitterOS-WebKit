/*
 * RitterOS: when the screen refreshes (DisplayRefreshRitterOS.h).
 */

#include "config.h"
#include "DisplayRefreshRitterOS.h"

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include <cmath>
#include <wtf/Deque.h>
#include <wtf/NeverDestroyed.h>

// RitterOS's libritteros (ritteros.h, not included: its names clash with
// WebKit's): wait at most timeout_ms for the screen's next vertical blank;
// 1 (its number in *frame), 0 when none came, -1 where no driver tells it.
extern "C" int ritteros_display_vblank(uint64_t* frame, uint32_t timeoutMilliseconds);

namespace WebCore {

DisplayRefreshRitterOS& DisplayRefreshRitterOS::singleton()
{
    static NeverDestroyed<DisplayRefreshRitterOS> refresh;
    return refresh;
}

void DisplayRefreshRitterOS::addUser()
{
    Locker locker { m_lock };
    if (m_unavailable)
        return;
    m_users++;
    m_condition.notifyAll();
    if (!m_thread) {
        m_thread = Thread::create("Display refresh"_s, [this] {
            threadMain();
        });
        m_thread->detach(); // (it lives as long as the process, waiting while unused)
    }
}

void DisplayRefreshRitterOS::removeUser()
{
    Locker locker { m_lock };
    if (m_users)
        m_users--;
}

std::optional<DisplayRefreshRitterOS::Refresh> DisplayRefreshRitterOS::refreshAfter(MonotonicTime time) const
{
    Locker locker { m_lock };
    // Known: watched, and lately (a mode change or a sleep stops them).
    if (!m_users || !m_period || MonotonicTime::now() - m_lastSeen > 100_ms)
        return std::nullopt;
    double periods = std::ceil((time - m_grid) / m_period);
    return Refresh { m_grid + m_period * periods, m_period };
}

void DisplayRefreshRitterOS::threadMain()
{
    // The last refreshes seen (about two seconds), oldest first.
    static constexpr size_t fitSize = 120;
    Deque<std::pair<uint64_t, MonotonicTime>> seen;
    while (true) {
        {
            Locker locker { m_lock };
            while (!m_users) {
                seen.clear();
                m_condition.wait(m_lock);
            }
        }
        uint64_t frame = 0;
        int result = ritteros_display_vblank(&frame, 100);
        auto now = MonotonicTime::now();
        if (result < 0) {
            Locker locker { m_lock };
            m_unavailable = true; // (no driver tells them: the player's clock alone)
            m_users = 0;
            m_thread = nullptr;
            return;
        }
        if (result != 1)
            continue;
        // A gap (a mode change, a missed second) starts the line again.
        if (!seen.isEmpty() && (frame <= seen.last().first || frame - seen.last().first > 30))
            seen.clear();
        seen.append({ frame, now });
        if (seen.size() > fitSize)
            seen.removeFirst();
        if (seen.size() < 10)
            continue;
        // Least squares of time over frame, relative to the newest.
        double n = seen.size(), sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        for (auto& [f, t] : seen) {
            double x = -static_cast<double>(frame - f);
            double y = (t - now).seconds();
            sumX += x;
            sumY += y;
            sumXX += x * x;
            sumXY += x * y;
        }
        double slope = (n * sumXY - sumX * sumY) / (n * sumXX - sumX * sumX);
        double intercept = (sumY - slope * sumX) / n;
        if (!(slope > 0.002 && slope < 0.1))
            continue; // (not a screen's refresh: 10-500 Hz)
        Locker locker { m_lock };
        m_period = Seconds(slope);
        m_grid = now + Seconds(intercept);
        m_lastSeen = now;
    }
}

} // namespace WebCore

#endif

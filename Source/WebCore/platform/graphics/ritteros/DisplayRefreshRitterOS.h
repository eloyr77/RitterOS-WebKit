/*
 * RitterOS: when the screen refreshes, for showing video pictures in step
 * with it.
 *
 * windowd shows a change at the screen's next vertical blank (it flips
 * between two screen buffers there, OS item 68t). A picture handed on at
 * the moment it is due reaches the screen 1 to 16 ms later, depending on
 * where the refresh falls; as the two clocks drift apart (the VivoBook's
 * refresh moves against its clock by a frame every ~40 s), pictures due
 * close to a refresh land on one side of it or the other, and stay on the
 * screen 1 or 3 refreshes instead of 2: an uneven picture. Knowing the
 * refreshes, the player picks for each refresh the picture due then, and
 * hands it on a fixed time before it.
 *
 * A thread waits for the vertical blanks (ritteros_display_vblank, which
 * any process may) while a video plays; where the system has no graphics
 * driver that tells them (a virtual machine) nothing is known and the
 * player times pictures by its clock alone.
 */

#pragma once

#if ENABLE(VIDEO) && PLATFORM(RITTEROS)

#include <optional>
#include <wtf/Condition.h>
#include <wtf/Lock.h>
#include <wtf/MonotonicTime.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/Seconds.h>
#include <wtf/Threading.h>

namespace WebCore {

class DisplayRefreshRitterOS {
public:
    static DisplayRefreshRitterOS& singleton();

    // A playing video: refreshes are watched while there is at least one.
    void addUser();
    void removeUser();

    struct Refresh {
        MonotonicTime time;
        Seconds period;
    };
    // The first refresh at or after time, if the refreshes are known now.
    std::optional<Refresh> refreshAfter(MonotonicTime) const;

private:
    friend class NeverDestroyed<DisplayRefreshRitterOS>;
    DisplayRefreshRitterOS() = default;
    void threadMain();

    mutable Lock m_lock;
    Condition m_condition;
    RefPtr<Thread> m_thread;
    unsigned m_users WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    bool m_unavailable WTF_GUARDED_BY_LOCK(m_lock) { false };
    // A refresh's time and the time between two, measured on this clock: a
    // straight line through the last refreshes seen (frame number, time
    // the thread woke), as the wake-ups come up to a millisecond late.
    MonotonicTime m_grid WTF_GUARDED_BY_LOCK(m_lock);
    Seconds m_period WTF_GUARDED_BY_LOCK(m_lock);
    MonotonicTime m_lastSeen WTF_GUARDED_BY_LOCK(m_lock);
};

} // namespace WebCore

#endif

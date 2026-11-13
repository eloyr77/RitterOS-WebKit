/*
 * RitterOS: abort() with a backtrace. RitterOS reports a program's SIGABRT
 * only as its exit code (134), so a program of the port (the jsc shell,
 * Compass's processes) links this file to define abort() itself: ld.so
 * binds every library's abort to the program's, which prints where it was
 * called from (each frame as LIBRARY+OFFSET, to look up with
 * `lldb FILE -o "image lookup -a OFFSET"`), then ends the program as
 * abort would.
 */

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <unwind.h>

namespace {

struct TraceState {
    int frames { 0 };
};

_Unwind_Reason_Code printFrame(struct _Unwind_Context* context, void* data)
{
    auto* state = static_cast<TraceState*>(data);
    uintptr_t pc = _Unwind_GetIP(context);
    if (!pc || state->frames >= 64)
        return _URC_END_OF_STACK;
    Dl_info info { };
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* name = info.dli_fname;
        for (const char* p = info.dli_fname; *p; ++p) {
            if (*p == '/')
                name = p + 1;
        }
        dprintf(2, "  #%-2d %s+0x%lx %s\n", state->frames, name,
            static_cast<unsigned long>(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)),
            info.dli_sname ? info.dli_sname : "");
    } else
        dprintf(2, "  #%-2d 0x%lx\n", state->frames, static_cast<unsigned long>(pc));
    state->frames++;
    return _URC_NO_REASON;
}

} // namespace

extern "C" __attribute__((visibility("default"), noreturn)) void abort(void)
{
    static volatile sig_atomic_t inAbort = 0;
    if (!inAbort) {
        inAbort = 1;
        dprintf(2, "abort() called from:\n");
        TraceState state;
        _Unwind_Backtrace(printFrame, &state);
    }
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
    _exit(134);
}

/*
 * RitterOS: abort() with a backtrace. RitterOS reports a program's SIGABRT
 * only as its exit code (134), so a program of the port (the jsc shell,
 * Compass's processes) links this file to define abort() itself: ld.so
 * binds every library's abort to the program's, which prints where it was
 * called from (each frame as LIBRARY+OFFSET, to look up with
 * `lldb FILE -o "image lookup -a OFFSET"`), then ends the program as
 * abort would. A fault (SIGSEGV, SIGBUS, SIGILL, SIGFPE) prints where it
 * happened and the address, then the backtrace from there, and ends the
 * program as the fault would have (the kernel log tells only the bare pc).
 */

#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
#include <sys/ucontext.h>
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

void printAddress(const char* what, uintptr_t pc)
{
    Dl_info info { };
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* name = strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname;
        dprintf(2, "%s %s+0x%lx %s\n", what, name,
            static_cast<unsigned long>(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)),
            info.dli_sname ? info.dli_sname : "");
    } else
        dprintf(2, "%s 0x%lx\n", what, static_cast<unsigned long>(pc));
}

void faultHandler(int signal, siginfo_t* info, void* context)
{
    auto* uc = static_cast<ucontext_t*>(context);
#if defined(__x86_64__)
    uintptr_t pc = uc->uc_mcontext.gregs[REG_RIP];
#else
    uintptr_t pc = uc->uc_mcontext.pc;
#endif
    dprintf(2, "fault: signal %d at address 0x%lx (pid %d, os thread %llx)\n", signal,
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(info->si_addr)), static_cast<int>(getpid()),
        (unsigned long long)reinterpret_cast<uint64_t>(pthread_self()));
    printAddress("  in", pc);
#if defined(__x86_64__)
    {
        auto& g = uc->uc_mcontext.gregs;
        dprintf(2, "  rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx\n  r8 %llx r9 %llx r12 %llx r13 %llx r14 %llx r15 %llx\n",
            (unsigned long long)g[REG_RAX], (unsigned long long)g[REG_RBX], (unsigned long long)g[REG_RCX], (unsigned long long)g[REG_RDX],
            (unsigned long long)g[REG_RSI], (unsigned long long)g[REG_RDI], (unsigned long long)g[REG_R8], (unsigned long long)g[REG_R9],
            (unsigned long long)g[REG_R12], (unsigned long long)g[REG_R13], (unsigned long long)g[REG_R14], (unsigned long long)g[REG_R15]);
        // What rbx points at (an object being destroyed, often), from 32
        // bytes before it (the allocator's header), if it looks like memory.
        uintptr_t object = g[REG_RBX];
        if (object > 0x10000 && !(object & 7)) {
            auto* words = reinterpret_cast<unsigned long long*>(object - 32);
            for (int i = 0; i < 12; i += 4)
                dprintf(2, "  [rbx%+d] %llx %llx %llx %llx\n", i * 8 - 32, words[i], words[i + 1], words[i + 2], words[i + 3]);
        }
    }
#else
    {
        auto& r = uc->uc_mcontext.regs;
        for (int i = 0; i < 31; i += 4)
            dprintf(2, "  x%-2d %llx %llx %llx %llx\n", i, (unsigned long long)r[i], (unsigned long long)r[i + 1],
                (unsigned long long)(i + 2 < 31 ? r[i + 2] : 0), (unsigned long long)(i + 3 < 31 ? r[i + 3] : 0));
        uintptr_t object = r[19];
        if (object > 0x10000 && !(object & 7)) {
            auto* words = reinterpret_cast<unsigned long long*>(object - 32);
            for (int i = 0; i < 16; i += 4)
                dprintf(2, "  [x19%+d] %llx %llx %llx %llx\n", i * 8 - 32, words[i], words[i + 1], words[i + 2], words[i + 3]);
        }
        uintptr_t target = reinterpret_cast<uintptr_t>(info->si_addr) & ~static_cast<uintptr_t>(15);
        if (target > 0x10000) {
            auto* words = reinterpret_cast<unsigned long long*>(target);
            dprintf(2, "  [fault] %llx %llx %llx %llx\n", words[0], words[1], words[2], words[3]);
        }
    }
#endif
    // The callers by the frame pointers (WebKit keeps them): the unwinder
    // does not step out of a signal handler here.
#if defined(__x86_64__)
    uintptr_t fp = uc->uc_mcontext.gregs[REG_RBP];
    uintptr_t sp = uc->uc_mcontext.gregs[REG_RSP];
#else
    uintptr_t fp = uc->uc_mcontext.regs[29];
    uintptr_t sp = uc->uc_mcontext.sp;
#endif
    dprintf(2, "  called from (sp 0x%lx):\n", static_cast<unsigned long>(sp));
    for (int i = 0; i < 40 && fp && !(fp & 7) && fp >= sp && fp - sp < (64u << 20); i++) {
        uintptr_t* frame = reinterpret_cast<uintptr_t*>(fp);
        char label[16];
        snprintf(label, sizeof(label), "  #%d", i);
        printAddress(label, frame[1]);
        if (frame[0] <= fp)
            break;
        fp = frame[0];
    }
    ::signal(signal, SIG_DFL);
    raise(signal);
    _exit(128 + signal);
}

__attribute__((constructor)) void installFaultHandlers()
{
    struct sigaction action { };
    action.sa_sigaction = faultHandler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
    for (int signal : signals)
        sigaction(signal, &action, nullptr);
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


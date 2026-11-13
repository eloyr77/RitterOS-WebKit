/*
 * RitterOS: a sampling profiler of a program's main thread, for finding
 * where WebKit's processes spend their time (RitterOS has no profiler of
 * its own). Off unless COMPASS_PROFILE names a folder: then a thread of
 * its own sends the main thread SIGPROF 200 times a second, the handler
 * records where it was and its callers (the frame pointers: WebKit, the
 * LLInt and JIT code keep them) and the thread writes the samples to
 * FOLDER/profile-PROGRAM-PID.txt:
 *
 *   M BASE PATH          a loaded object (again when more were loaded)
 *   T SECONDS            the time since the start, once a second
 *   S PC RETURN...       a sample: where the thread was, then its callers
 *
 * tools/profile.py on the Mac names the functions (the build's symbols).
 * RitterOS restarts no interrupted call (SA_RESTART does nothing there):
 * a wait of the main thread may end early with EINTR, as with JSC's
 * thread suspension, so this is for measuring, not for everyday use.
 */

#include <atomic>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr int samplesPerSecond = 200;
constexpr int maxFrames = 32;
constexpr unsigned ringSize = 4096; // samples between two writes (20 s at 200 Hz)

struct Sample {
    unsigned count;
    uintptr_t frames[maxFrames];
};

Sample ring[ringSize];
std::atomic<unsigned> written { 0 }; // samples the handler has finished (only it writes)
pthread_t profiledThread;
uintptr_t stackLow, stackHigh;       // the main thread's stack: frames are only read there

void sampleHandler(int, siginfo_t*, void* context)
{
    auto* uc = static_cast<ucontext_t*>(context);
#if defined(__x86_64__)
    uintptr_t pc = uc->uc_mcontext.gregs[REG_RIP];
    uintptr_t fp = uc->uc_mcontext.gregs[REG_RBP];
#else
    uintptr_t pc = uc->uc_mcontext.pc;
    uintptr_t fp = uc->uc_mcontext.regs[29];
#endif
    unsigned index = written.load(std::memory_order_relaxed);
    Sample& sample = ring[index % ringSize];
    unsigned n = 0;
    sample.frames[n++] = pc;
    while (n < maxFrames && fp >= stackLow && fp + 16 <= stackHigh && !(fp & 7)) {
        auto* frame = reinterpret_cast<uintptr_t*>(fp);
        if (!frame[1])
            break;
        sample.frames[n++] = frame[1];
        if (frame[0] <= fp)
            break;
        fp = frame[0];
    }
    sample.count = n;
    written.store(index + 1, std::memory_order_release);
}

int writeObject(struct dl_phdr_info* info, size_t, void* data)
{
    auto* file = static_cast<FILE*>(data);
    // (the program itself has no name in the list: its own)
    const char* name = info->dlpi_name && *info->dlpi_name ? info->dlpi_name : getprogname();
    if (name && *name)
        fprintf(file, "M %lx %s\n", static_cast<unsigned long>(info->dlpi_addr), name);
    return 0;
}

int countObject(struct dl_phdr_info*, size_t, void* data)
{
    ++*static_cast<int*>(data);
    return 0;
}

double now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void* samplerThread(void* data)
{
    auto* file = static_cast<FILE*>(data);
    int objects = -1;
    unsigned read = 0;
    double start = now(), lastSecond = 0;
    struct timespec interval { 0, 1000000000L / samplesPerSecond };
    while (true) {
        nanosleep(&interval, nullptr);
        pthread_kill(profiledThread, SIGPROF);
        double elapsed = now() - start;
        if (elapsed - lastSecond < 1)
            continue;
        lastSecond = elapsed;
        int count = 0;
        dl_iterate_phdr(countObject, &count);
        if (count != objects) {
            objects = count;
            dl_iterate_phdr(writeObject, file);
        }
        fprintf(file, "T %.0f\n", elapsed);
        unsigned end = written.load(std::memory_order_acquire);
        if (end - read > ringSize)
            read = end - ringSize; // (fell behind: the oldest are gone)
        for (; read != end; read++) {
            const Sample& sample = ring[read % ringSize];
            fputc('S', file);
            for (unsigned i = 0; i < sample.count; i++)
                fprintf(file, " %lx", static_cast<unsigned long>(sample.frames[i]));
            fputc('\n', file);
        }
        fflush(file);
    }
    return nullptr;
}

} // namespace

// Called by the program's main (WebKit's WebProcessMainRitterOS.cpp), not as
// a constructor: RitterOS's ld.so runs constructors before the C library
// has the environment, where getenv finds nothing.
extern "C" void ritterosStartProfiler()
{
    const char* folder = getenv("COMPASS_PROFILE");
    if (!folder || !*folder)
        return;
    profiledThread = pthread_self();
    pthread_attr_t attributes;
    void* stack = nullptr;
    size_t stackSize = 0;
    if (int error = pthread_getattr_np(profiledThread, &attributes)) {
        dprintf(2, "profiler: no stack for the main thread (%d)\n", error);
        return;
    }
    pthread_attr_getstack(&attributes, &stack, &stackSize);
    pthread_attr_destroy(&attributes);
    stackLow = reinterpret_cast<uintptr_t>(stack);
    stackHigh = stackLow + stackSize;

    const char* program = getprogname();
    char path[512];
    snprintf(path, sizeof(path), "%s/profile-%s-%d.txt", folder, program ? program : "program", static_cast<int>(getpid()));
    FILE* file = fopen(path, "w");
    if (!file) {
        dprintf(2, "profiler: cannot write %s\n", path);
        return;
    }
    fprintf(file, "# %s pid %d: its main thread, %d samples a second\n", program ? program : "", static_cast<int>(getpid()), samplesPerSecond);

    struct sigaction action { };
    action.sa_sigaction = sampleHandler;
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGPROF, &action, nullptr);
    pthread_t thread;
    if (!pthread_create(&thread, nullptr, samplerThread, file))
        pthread_detach(thread);
    dprintf(2, "profiler: sampling the main thread into %s\n", path);
}

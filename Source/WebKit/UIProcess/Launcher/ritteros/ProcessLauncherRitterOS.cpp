/*
 * RitterOS: starting WebKit's helper programs with posix_spawn. Modeled on UIProcess/Launcher/playstation/ProcessLauncherPlayStation.cpp (socket pair,
 * SOCK_SEQPACKET, buffer sizes) and glib/ProcessLauncherGLib.cpp:146-198 (executable path,
 * argv). The child's command line is what Shared/unix/AuxiliaryProcessMain.cpp:62-90 parses:
 *     argv[0] = executable, argv[1] = processIdentifier, argv[2] = connection fd
 *     [, --configure-jsc-for-testing]
 */

#include "config.h"
#include "ProcessLauncher.h"

#include "IPCUtilities.h"
#include <WebCore/ProcessIdentifier.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wtf/FileSystem.h>
#include <wtf/RunLoop.h>
#include <wtf/Threading.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>

extern char** environ;

template<typename T> static char* toCharPointer(const T& string)
{
    return reinterpret_cast<char*>(const_cast<char8_t*>(string.data()));
}

namespace WebKit {

static String auxiliaryProcessPath(ProcessLauncher::ProcessType processType)
{
    // RITTEROS_WEBKIT_EXEC_PATH overrides the install directory (like WEBKIT_EXEC_PATH on GLib).
    // Else the helpers are in the app's bundle, beside the program, one folder above
    // the bundle's lib/ that holds this library (APP.program/lib/libWebKit.so).
    String directory;
    if (const char* override = getenv("RITTEROS_WEBKIT_EXEC_PATH"))
        directory = String::fromUTF8(override);
    else {
        Dl_info info { };
        if (dladdr(reinterpret_cast<void*>(&auxiliaryProcessPath), &info) && info.dli_fname)
            directory = FileSystem::parentPath(FileSystem::parentPath(String::fromUTF8(info.dli_fname)));
        if (directory.isEmpty())
            directory = String::fromUTF8(RITTEROS_LIBEXECDIR); // cmakeconfig.h, OptionsRitterOS.cmake
    }

    switch (processType) {
    case ProcessLauncher::ProcessType::Network:
        return FileSystem::pathByAppendingComponent(directory, "WebKitNetworkProcess"_s);
    case ProcessLauncher::ProcessType::Web:
    default:
        return FileSystem::pathByAppendingComponent(directory, "WebKitWebProcess"_s);
    }
}

void ProcessLauncher::launchProcess()
{
    // Server end close-on-exec, client end inherited (IPCUtilitiesUnix.cpp:34-60).
    IPC::SocketPair socketPair = IPC::createPlatformConnection(SOCK_SEQPACKET, IPC::PlatformConnectionOptions::SetCloexecOnServer);

    int bufferSize = 32 * 1024;
    setsockopt(socketPair.server.value(), SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));
    setsockopt(socketPair.client.value(), SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));
    setsockopt(socketPair.server.value(), SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));
    setsockopt(socketPair.client.value(), SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));

    auto executable = FileSystem::fileSystemRepresentation(auxiliaryProcessPath(m_launchOptions.processType));
    auto processIdentifier = String::number(m_launchOptions.processIdentifier.toUInt64()).utf8();
    auto connectionFD = String::number(socketPair.client.value()).utf8();

    Vector<char*, 5> argv;
    argv.append(toCharPointer(executable));
    argv.append(toCharPointer(processIdentifier));
    argv.append(toCharPointer(connectionFD));
#if ENABLE(DEVELOPER_MODE)
    if (m_launchOptions.processType == ProcessLauncher::ProcessType::Web && m_client && m_client->shouldConfigureJSCForTesting())
        argv.append(const_cast<char*>("--configure-jsc-for-testing"));
#endif
    argv.append(nullptr);

    // Every other WebKit fd is close-on-exec (IPC sets FD_CLOEXEC on received fds,
    // ConnectionUnix.cpp:273-278); launches happen on the main thread, so no other
    // client socket can be leaked into this child.
    pid_t pid = 0;
    int result = posix_spawn(&pid, toCharPointer(executable), nullptr, nullptr, argv.mutableSpan().data(), environ);

    // The child has its copy; the parent must not keep the client end open, or the
    // server end never sees EOF when the child dies.
    socketPair.client = { };

    if (result) {
        WTFLogAlways("ProcessLauncher: posix_spawn(%s) failed: %d", toCharPointer(executable), result);
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }] {
            protectedThis->didFinishLaunchingProcess(0, { });
        });
        return;
    }

    // Reap the child when it exits (there is no GSubprocess here). The IPC connection
    // notices the exit by itself: the socket reports EOF (ConnectionUnix.cpp:308-311).
    Thread::create("WebKit child reaper"_s, [pid] {
        int status;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) { }
    })->detach();

    RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, pid, serverSocket = WTF::move(socketPair.server)] mutable {
        protectedThis->didFinishLaunchingProcess(pid, IPC::Connection::Identifier { WTF::move(serverSocket) });
    });
}

void ProcessLauncher::terminateProcess(const String&)
{
    if (m_isLaunching) {
        invalidate();
        return;
    }
    if (!m_processID)
        return;
    kill(m_processID, SIGKILL);
    m_processID = 0;
}

void ProcessLauncher::platformInvalidate()
{
}

} // namespace WebKit

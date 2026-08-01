#include "n2CardReader.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "../../../config/config.h"
#include "../../../log/log.h"

/*
 * The cabinet's card reader/writer hangs off /dev/ttyM2; here that line is a
 * named pipe served by an external YaCardEmu.  The game polls the port from
 * its sequence threads and asks for the device state once per frame, so
 * nothing on the game's side may ever block: a single Win32 call that waits -
 * CreateFile on a busy pipe, WaitNamedPipe, a blocking ReadFile - is enough to
 * stall rendering if it happens under a lock the frame path also takes.
 *
 * So the pipe is owned by one background thread.  It connects, reconnects and
 * moves bytes; the game-facing entry points only ever touch two byte queues
 * and an atomic state flag, and hold the lock just long enough to splice
 * bytes in or out.  No Win32 blocking call is made while the lock is held.
 */

namespace
{
constexpr int cardDescriptor = 0x7202;
constexpr char cardDevicePath[] = "/dev/ttyM2";
constexpr DWORD reconnectIntervalMs = 1000;
// Plenty for the reader's framing; a stuck consumer must not grow this without
// bound, and dropping the oldest bytes of a dead conversation is harmless.
constexpr size_t maximumQueuedBytes = 64 * 1024;

enum class LinkState
{
    Disconnected,
    Connected,
};

std::mutex bufferMutex;
std::deque<uint8_t> receiveQueue; // reader -> game
std::deque<uint8_t> transmitQueue; // game -> reader
std::atomic<LinkState> linkState{LinkState::Disconnected};
std::atomic<bool> workerRunning{false};
std::atomic<bool> workerStopRequested{false};
std::thread workerThread;
std::once_flag workerOnce;
bool launchAttempted = false;
bool connectFailureReported = false;

bool launchYaCardEmu()
{
    EmulatorConfig *config = getConfig();
    if (!config->n2YaCardEmuAutoStart || !config->n2YaCardEmuPath[0] || launchAttempted)
        return false;

    launchAttempted = true;
    std::filesystem::path executable(config->n2YaCardEmuPath);
    std::string commandLine = "\"" + executable.string() + "\"";
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    std::string workingDirectory = executable.parent_path().string();

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const BOOL started = CreateProcessA(
        executable.string().c_str(), mutableCommand.data(), nullptr, nullptr,
        FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo, &processInfo);
    if (!started)
    {
        log_error("Namco N2 card: failed to launch external YaCardEmu (%s, error=%lu)",
                  executable.string().c_str(), GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    log_info("Namco N2 card: launched external YaCardEmu process");
    return true;
}

// Runs on the worker thread only, so it is free to block.
HANDLE openCardPipe()
{
    EmulatorConfig *config = getConfig();
    const char *pipeName = config->n2YaCardEmuPipe;
    if (!pipeName[0])
        return INVALID_HANDLE_VALUE;

    HANDLE pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND)
        launchYaCardEmu();

    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY &&
        WaitNamedPipeA(pipeName, 200))
        pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        if (!connectFailureReported)
        {
            connectFailureReported = true;
            log_warn("Namco N2 card: YaCardEmu pipe is unavailable: %s (error=%lu). "
                     "The cabinet will report E51 until it is running.",
                     pipeName, GetLastError());
        }
        return INVALID_HANDLE_VALUE;
    }

    /*
     * Byte mode, and deliberately not PIPE_NOWAIT: the worker owns this handle
     * and is allowed to block, while a non-blocking pipe would hand back short
     * writes that are easy to drop silently.  Reads are always preceded by a
     * PeekNamedPipe, so ReadFile never waits either.
     */
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    connectFailureReported = false;
    log_info("Namco N2 card: connected /dev/ttyM2 to external YaCardEmu at %s", pipeName);
    return pipe;
}

/*
 * Reader frames are STX, LEN, payload..., ETX, BCC, where LEN counts every
 * byte from the command through the BCC, so a frame is LEN + 2 bytes long.
 * ACK/NACK/ENQ travel as single bytes.  Returns 0 while the head of the buffer
 * is still an incomplete frame.
 */
size_t framedLength(const std::deque<uint8_t> &bytes)
{
    if (bytes.empty())
        return 0;
    if (bytes.front() != 0x02)
        return 1;
    if (bytes.size() < 2)
        return 0;

    const size_t length = static_cast<size_t>(bytes[1]) + 2;
    if (length < 4)
        return 1; // Not a frame the reader can produce; hand the byte over as-is.
    return bytes.size() < length ? 0 : length;
}

/*
 * Cancel (0x40) leaves YaCardEmu reporting ILLEGAL_COMMAND, and it keeps
 * reporting it on every following poll until some other command clears it.
 * That is fatal for the "no card" branch of the card-have screen: after the
 * player picks NO, clSeqCardHaveSelectThread::runSelect() cancels the pending
 * insert and only leaves its wait loop when the cancel finishes with a plain
 * job-end result.  ILLEGAL_COMMAND maps to CANT EXEC ERR, which sends it
 * straight back to re-arming the insert, so the new-card sequence - and with
 * it clCardDeviceGameService::requestCreate(), the only caller that ever asks
 * the dispenser for a blank card - is never reached.
 *
 * The cabinet's reader answers a cancel that actually aborted a command with
 * NO_JOB, so that is what the game is handed here.  Only the status reply to a
 * cancel is touched; every other frame passes through untouched.
 */
void neutraliseCancelStatus(std::vector<uint8_t> &frame)
{
    constexpr uint8_t cancelCommand = 0x40;
    constexpr uint8_t illegalCommand = 0x32;
    constexpr uint8_t noJob = 0x30;

    if (frame.size() != 8 || frame[0] != 0x02 || frame[1] != 0x06 ||
        frame[2] != cancelCommand || frame[6] != 0x03 || frame[5] != illegalCommand)
        return;

    frame[5] = noJob;
    frame[7] ^= illegalCommand ^ noJob; // The BCC is an XOR over LEN..ETX.
}

void dropQueues()
{
    /*
     * A reconnect starts a fresh conversation.  Carrying half of the previous
     * one across would leave the game parsing a reply to a command the new
     * reader never saw, which is the kind of desync that only clears by
     * restarting both sides.
     */
    std::lock_guard<std::mutex> lock(bufferMutex);
    receiveQueue.clear();
    transmitQueue.clear();
}

/*
 * Reader replies have to arrive at the speed the cabinet's line runs at.
 * clCardPrinter keeps the last status byte it saw and samples that cache when
 * the game asks it to do something; a named pipe hands a whole reply over in
 * one go, so a status the reader only holds briefly - notably the
 * ILLEGAL_COMMAND that real hardware raises in response to Cancel - lands in
 * the cache far more often than it would at 38400 baud.  That is what makes
 * the new-card path see "cannot execute" on every attempt.
 *
 * etc/config.ini runs the emulator at 38400 8N1, i.e. ten bits per byte.
 */
constexpr double serialBytesPerMillisecond = 38400.0 / 10.0 / 1000.0;

double millisecondsSince(LARGE_INTEGER &previous, const LARGE_INTEGER &frequency)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double elapsed = static_cast<double>(now.QuadPart - previous.QuadPart) * 1000.0 /
                           static_cast<double>(frequency.QuadPart);
    previous = now;
    return elapsed;
}

void cardWorker()
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    DWORD lastConnectAttempt = 0;

    // Bytes off the pipe that have not been split into frames yet, and frames
    // waiting to be released to the game at line rate.
    std::deque<uint8_t> incoming;
    std::deque<uint8_t> pending;
    double byteCredit = 0.0;
    LARGE_INTEGER frequency;
    LARGE_INTEGER lastPace;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastPace);

    while (!workerStopRequested.load(std::memory_order_relaxed))
    {
        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (!getConfig()->n2YaCardEmuEnabled)
            {
                Sleep(200);
                continue;
            }

            const DWORD now = GetTickCount();
            if (lastConnectAttempt != 0 &&
                static_cast<LONG>(now - lastConnectAttempt) < static_cast<LONG>(reconnectIntervalMs))
            {
                Sleep(50);
                continue;
            }

            lastConnectAttempt = now;
            pipe = openCardPipe();
            if (pipe == INVALID_HANDLE_VALUE)
                continue;

            dropQueues();
            linkState.store(LinkState::Connected, std::memory_order_release);
            continue;
        }

        bool failed = false;

        // Flush whatever the game handed us since the last pass.
        std::vector<uint8_t> outgoing;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (!transmitQueue.empty())
            {
                outgoing.assign(transmitQueue.begin(), transmitQueue.end());
                transmitQueue.clear();
            }
        }
        if (!outgoing.empty())
        {
            DWORD written = 0;
            if (!WriteFile(pipe, outgoing.data(), static_cast<DWORD>(outgoing.size()), &written, nullptr))
            {
                failed = true;
            }
            if (!failed && written < outgoing.size())
            {
                // Put the tail back at the head of the queue so a short write
                // cannot silently truncate a command frame.
                std::lock_guard<std::mutex> lock(bufferMutex);
                transmitQueue.insert(transmitQueue.begin(), outgoing.begin() + written, outgoing.end());
            }
        }

        // Collect anything the reader has sent back.
        if (!failed)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
            {
                failed = true;
            }
            else if (available)
            {
                uint8_t chunk[1024];
                const DWORD wanted = available > sizeof(chunk) ? sizeof(chunk) : available;
                DWORD read = 0;
                if (!ReadFile(pipe, chunk, wanted, &read, nullptr))
                {
                    failed = true;
                }
                else if (read)
                {
                    incoming.insert(incoming.end(), chunk, chunk + read);
                    for (size_t length = framedLength(incoming); length != 0;
                         length = framedLength(incoming))
                    {
                        std::vector<uint8_t> frame(incoming.begin(), incoming.begin() + length);
                        incoming.erase(incoming.begin(), incoming.begin() + length);
                        neutraliseCancelStatus(frame);
                        if (pending.size() + frame.size() <= maximumQueuedBytes)
                            pending.insert(pending.end(), frame.begin(), frame.end());
                    }

                    /*
                     * A length byte that never resolves would otherwise hold
                     * the conversation forever; past that point the framing is
                     * already lost, so let the game resynchronise on the bytes
                     * themselves.
                     */
                    if (incoming.size() > maximumQueuedBytes)
                    {
                        pending.insert(pending.end(), incoming.begin(), incoming.end());
                        incoming.clear();
                    }
                }
            }
        }

        if (failed)
        {
            log_warn("Namco N2 card: YaCardEmu closed the pipe; the cabinet will report E51");
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            linkState.store(LinkState::Disconnected, std::memory_order_release);
            connectFailureReported = true;
            incoming.clear();
            pending.clear();
            byteCredit = 0.0;
            dropQueues();
            continue;
        }

        // Hand the game its bytes no faster than the cabinet line would.
        byteCredit += millisecondsSince(lastPace, frequency) * serialBytesPerMillisecond;
        if (!pending.empty() && byteCredit >= 1.0)
        {
            size_t release = static_cast<size_t>(byteCredit);
            if (release > pending.size())
                release = pending.size();
            byteCredit -= static_cast<double>(release);
            std::lock_guard<std::mutex> lock(bufferMutex);
            receiveQueue.insert(receiveQueue.end(), pending.begin(), pending.begin() + release);
            pending.erase(pending.begin(), pending.begin() + release);
        }
        else if (pending.empty())
        {
            // Do not bank credit across idle periods.
            byteCredit = 0.0;
        }

        Sleep(1);
    }

    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
    linkState.store(LinkState::Disconnected, std::memory_order_release);
    workerRunning.store(false, std::memory_order_release);
}

void ensureWorkerStarted()
{
    std::call_once(workerOnce, []() {
        workerRunning.store(true, std::memory_order_release);
        workerThread = std::thread(cardWorker);
        workerThread.detach();
    });
}

bool linkIsUp()
{
    ensureWorkerStarted();
    return linkState.load(std::memory_order_acquire) == LinkState::Connected;
}
} // namespace

extern "C" int n2CardReaderOpen(const char *path, int)
{
    if (!path || std::strcmp(path, cardDevicePath) != 0)
        return -1;

    if (!linkIsUp())
    {
        errno = ENODEV;
        return -1;
    }
    return cardDescriptor;
}

extern "C" int n2CardReaderIsConnected(void)
{
    return linkIsUp() ? 1 : 0;
}

extern "C" int n2CardReaderIsDescriptor(int fd)
{
    return fd == cardDescriptor;
}

extern "C" int n2CardReaderBytesAvailable(int fd)
{
    if (fd != cardDescriptor)
        return 0;

    std::lock_guard<std::mutex> lock(bufferMutex);
    return static_cast<int>(receiveQueue.size());
}

extern "C" int n2CardReaderRead(int fd, void *buffer, size_t count)
{
    if (fd != cardDescriptor || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    if (linkState.load(std::memory_order_acquire) != LinkState::Connected)
    {
        errno = ENODEV;
        return -1;
    }

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (receiveQueue.empty())
    {
        // clSerialN2::receive() reads EAGAIN as "nothing yet" and returns zero,
        // so this is the normal idle answer rather than an error.
        errno = EAGAIN;
        return -1;
    }

    const size_t taken = count < receiveQueue.size() ? count : receiveQueue.size();
    uint8_t *out = static_cast<uint8_t *>(buffer);
    for (size_t i = 0; i < taken; i++)
        out[i] = receiveQueue[i];
    receiveQueue.erase(receiveQueue.begin(), receiveQueue.begin() + taken);
    return static_cast<int>(taken);
}

extern "C" int n2CardReaderWrite(int fd, const void *buffer, size_t count)
{
    if (fd != cardDescriptor || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (linkState.load(std::memory_order_acquire) != LinkState::Connected)
    {
        errno = ENODEV;
        return -1;
    }

    if (count)
    {
        const uint8_t *in = static_cast<const uint8_t *>(buffer);
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (transmitQueue.size() + count > maximumQueuedBytes)
        {
            errno = EAGAIN;
            return -1;
        }
        transmitQueue.insert(transmitQueue.end(), in, in + count);
    }
    // The worker flushes on its next pass; the game only needs to know the
    // bytes were accepted, exactly as a buffered serial port would report.
    return static_cast<int>(count);
}

extern "C" int n2CardReaderClose(int fd)
{
    if (fd != cardDescriptor)
        return -1;

    /*
     * The game closes and reopens the port around some sequences.  The link
     * itself stays up - tearing the pipe down here would restart YaCardEmu's
     * conversation and lose whatever command was in flight.
     */
    return 0;
}

extern "C" int n2CardReaderIoctl(int fd, unsigned long request, void *argument)
{
    if (fd != cardDescriptor)
        return -1;

    /*
     * FIONREAD is how the card layer decides whether a reply is worth reading.
     * Reporting success without filling the count in leaves it believing the
     * port is permanently empty.
     */
    constexpr unsigned long linuxFionread = 0x541B;
    if (request == linuxFionread && argument)
    {
        *static_cast<int *>(argument) = n2CardReaderBytesAvailable(fd);
        return 0;
    }

    // clSerialN2 otherwise uses Linux serial-specific RS485 ioctls; the pipe is
    // already full duplex, so no host-side operation is required.
    return 0;
}

#endif

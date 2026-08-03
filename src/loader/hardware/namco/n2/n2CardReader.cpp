#include "n2CardReader.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "../../../config/config.h"
#include "../../common/cardControl.h"
#include "../../../log/log.h"

/*
 * The card reader on /dev/ttyM2, here a named pipe served by an external
 * YaCardEmu. The game polls it from the frame path, so nothing on its side may
 * block - one waiting Win32 call under a lock the frame path also takes is
 * enough to stall rendering.
 *
 * So a background thread owns the pipe and does all the connecting and moving
 * of bytes. The game-facing entry points touch two byte queues and an atomic
 * flag, and no blocking call is ever made while the lock is held.
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

enum class ControlRequest
{
    Insert,
    Eject,
};

std::mutex bufferMutex;
std::deque<uint8_t> receiveQueue; // reader -> game
std::deque<uint8_t> transmitQueue; // game -> reader
std::atomic<LinkState> linkState{LinkState::Disconnected};
std::atomic<bool> workerRunning{false};
std::atomic<bool> workerStopRequested{false};
std::thread workerThread;
std::once_flag workerOnce;
std::once_flag controlWorkerOnce;
bool launchAttempted = false;
bool connectFailureReported = false;

std::mutex controlMutex;
std::deque<ControlRequest> controlQueue;
std::atomic<bool> apiConnected{false};
std::atomic<bool> cardInserted{false};

void logFrame(const char *direction, const uint8_t *bytes, size_t size)
{
    if (!getConfig()->namcoN2.card.diagnostics || !bytes || !size)
        return;

    std::ostringstream line;
    line << "Namco N2 card " << direction << " [" << size << "]:";
    line << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; i++)
        line << ' ' << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    log_info("%s", line.str().c_str());
}

std::string urlEncode(const char *value)
{
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (const unsigned char c : std::string(value ? value : ""))
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            encoded << static_cast<char>(c);
        else
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(c);
    }
    return encoded.str();
}

bool httpRequest(const char *method, const std::string &path, std::string *body)
{
    const NamcoN2CardConfig &card = getConfig()->namcoN2.card;
    if (!card.apiHost[0] || card.apiPort <= 0 || card.apiPort > 65535)
        return false;

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port[16];
    std::snprintf(port, sizeof(port), "%d", card.apiPort);
    addrinfo *addresses = nullptr;
    if (getaddrinfo(card.apiHost, port, &hints, &addresses) != 0)
        return false;

    SOCKET socketHandle = INVALID_SOCKET;
    for (addrinfo *address = addresses; address; address = address->ai_next)
    {
        socketHandle = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socketHandle == INVALID_SOCKET)
            continue;

        u_long nonBlocking = 1;
        ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
        int connected = connect(socketHandle, address->ai_addr, static_cast<int>(address->ai_addrlen));
        if (connected == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(socketHandle, &writable);
            timeval timeout = {0, 500000};
            connected = select(0, nullptr, &writable, nullptr, &timeout) > 0 ? 0 : SOCKET_ERROR;
            if (connected == 0)
            {
                int error = 0;
                int length = sizeof(error);
                getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length);
                if (error)
                    connected = SOCKET_ERROR;
            }
        }
        if (connected == 0)
        {
            nonBlocking = 0;
            ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
            break;
        }
        closesocket(socketHandle);
        socketHandle = INVALID_SOCKET;
    }
    freeaddrinfo(addresses);
    if (socketHandle == INVALID_SOCKET)
        return false;

    DWORD timeoutMs = 750;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));

    std::ostringstream request;
    request << method << ' ' << path << " HTTP/1.0\r\nHost: " << card.apiHost
            << "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    const std::string requestText = request.str();
    size_t sentTotal = 0;
    while (sentTotal < requestText.size())
    {
        int sent = send(socketHandle, requestText.data() + sentTotal,
                        static_cast<int>(requestText.size() - sentTotal), 0);
        if (sent <= 0)
        {
            closesocket(socketHandle);
            return false;
        }
        sentTotal += static_cast<size_t>(sent);
    }

    std::string response;
    char chunk[2048];
    for (int received = recv(socketHandle, chunk, sizeof(chunk), 0); received > 0;
         received = recv(socketHandle, chunk, sizeof(chunk), 0))
        response.append(chunk, chunk + received);
    closesocket(socketHandle);

    const size_t firstSpace = response.find(' ');
    const int status = firstSpace == std::string::npos ? 0 : std::atoi(response.c_str() + firstSpace + 1);
    const size_t headerEnd = response.find("\r\n\r\n");
    if (body)
        *body = headerEnd == std::string::npos ? std::string() : response.substr(headerEnd + 4);
    return status >= 200 && status < 300;
}

bool refreshApiStatus(bool announce)
{
    std::string insertedBody;
    const bool okay = httpRequest("GET", "/api/v1/insertedCard", &insertedBody);
    const bool wasConnected = apiConnected.exchange(okay, std::memory_order_acq_rel);
    if (okay)
    {
        cardInserted.store(insertedBody.find("\"inserted\":true") != std::string::npos,
                           std::memory_order_release);
    }
    if (okay != wasConnected)
        log_info("Namco N2 card API: %s at http://%s:%d",
                 okay ? "connected" : "disconnected", getConfig()->namcoN2.card.apiHost,
                 getConfig()->namcoN2.card.apiPort);
    if (announce)
        log_info("Namco N2 card status: pipe=%s api=%s inserted=%s",
                 linkState.load() == LinkState::Connected ? "connected" : "disconnected",
                 okay ? "connected" : "disconnected",
                 cardInserted.load() ? "yes" : "no");
    return okay;
}

void performControlRequest(ControlRequest request)
{
    const char *cardName = getConfig()->namcoN2.card.cardName;
    std::string ignored;
    switch (request)
    {
        case ControlRequest::Insert:
        {
            std::string path = "/api/v1/insertedCard?loadonly=1";
            if (cardName[0])
                path += "&cardname=" + urlEncode(cardName);
            if (httpRequest("POST", path, &ignored))
                log_info("Namco N2 card: insert requested%s%s", cardName[0] ? " for " : "",
                         cardName[0] ? cardName : "");
            else
                log_warn("Namco N2 card: insert request failed; YaCardEmu API is unavailable");
            break;
        }
        case ControlRequest::Eject:
            if (httpRequest("DELETE", "/api/v1/insertedCard", &ignored))
                log_info("Namco N2 card: eject requested through YaCardEmu API");
            else
                log_warn("Namco N2 card: eject request failed; YaCardEmu API is unavailable");
            break;
    }
    refreshApiStatus(false);
    if (request == ControlRequest::Eject && apiConnected.load() && cardInserted.load())
        log_warn("Namco N2 card: YaCardEmu accepted DELETE but still reports the card inserted; "
                 "this YaCardEmu build does not implement forced removal, so let the game eject it");
}

void queueControlRequest(ControlRequest request)
{
    std::lock_guard<std::mutex> lock(controlMutex);
    if (controlQueue.size() < 32)
        controlQueue.push_back(request);
}

void cardControlWorker()
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        log_error("Namco N2 card API: WSAStartup failed");
        return;
    }

    DWORD lastPoll = 0;
    while (!workerStopRequested.load(std::memory_order_relaxed))
    {
        ControlRequest request = ControlRequest::Insert;
        bool haveRequest = false;
        {
            std::lock_guard<std::mutex> lock(controlMutex);
            if (!controlQueue.empty())
            {
                request = controlQueue.front();
                controlQueue.pop_front();
                haveRequest = true;
            }
        }
        const DWORD now = GetTickCount();
        if (haveRequest)
            performControlRequest(request);
        else if (static_cast<LONG>(now - lastPoll) >= 2000)
        {
            refreshApiStatus(false);
            lastPoll = now;
        }
        else
            Sleep(25);
    }
    WSACleanup();
}

bool launchYaCardEmu()
{
    EmulatorConfig *config = getConfig();
    if (!config->namcoN2.card.autoStart || !config->namcoN2.card.executablePath[0] ||
        launchAttempted)
        return false;

    launchAttempted = true;
    std::filesystem::path executable(config->namcoN2.card.executablePath);
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
    const char *pipeName = config->namcoN2.card.pipeName;
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
 * YaCardEmu answers Cancel (0x40) with ILLEGAL_COMMAND and keeps reporting it
 * until another command clears it. That hangs the "no card" branch:
 * runSelect() leaves its wait loop only on a plain job-end result, and
 * ILLEGAL_COMMAND maps to CANT EXEC ERR, which re-arms the insert instead - so
 * the new-card sequence is never reached.
 *
 * Real hardware answers a cancel that aborted a command with NO_JOB. Only the
 * status reply to a cancel is touched.
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
 * Replies are paced to the cabinet's line speed. clCardPrinter caches the last
 * status byte it saw, and a pipe delivering a whole reply at once puts a
 * briefly-held status into that cache far more often than 38400 baud would.
 *
 * etc/config.ini runs the emulator at 38400 8N1, ten bits per byte.
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
            if (!getConfig()->namcoN2.card.enabled)
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
            logFrame("TX", outgoing.data(), outgoing.size());
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
                        logFrame("RX", frame.data(), frame.size());
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
    std::call_once(controlWorkerOnce, []() {
        std::thread(cardControlWorker).detach();
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

extern "C" const char *n2CardReaderConnectionText(void)
{
    ensureWorkerStarted();
    if (linkState.load(std::memory_order_acquire) != LinkState::Connected)
        return "Disconnected";
    return apiConnected.load(std::memory_order_acquire) ? "Connected" : "Reader only";
}

extern "C" void n2CardReaderRequestInsert(void)
{
    ensureWorkerStarted();
    queueControlRequest(ControlRequest::Insert);
}

extern "C" void n2CardReaderRequestEject(void)
{
    ensureWorkerStarted();
    queueControlRequest(ControlRequest::Eject);
}

namespace
{
CardControlActionResult n2SetCardInsertState(int active)
{
    if (!active)
        return CARD_CONTROL_HANDLED;
    n2CardReaderRequestInsert();
    return CARD_CONTROL_HANDLED_ONE_SHOT;
}

CardControlActionResult n2RequestCardEject(void)
{
    n2CardReaderRequestEject();
    return CARD_CONTROL_HANDLED_ONE_SHOT;
}

CardControlConnectionState n2CardConnectionState(void)
{
    return n2CardReaderIsConnected()
               ? CARD_CONTROL_CONNECTED
               : CARD_CONTROL_DISCONNECTED;
}
}

extern "C" void n2CardReaderRegisterCardControl(void)
{
    const CardControlBackend backend = {
        "Namco N2 external YaCardEmu",
        n2SetCardInsertState,
        n2RequestCardEject,
        n2CardConnectionState,
        n2CardReaderConnectionText,
        n2CardReaderLogDiagnostics
    };
    cardControlSetBackend(&backend);
}

extern "C" void n2CardReaderLogDiagnostics(void)
{
    ensureWorkerStarted();
    size_t receiveBytes;
    size_t transmitBytes;
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        receiveBytes = receiveQueue.size();
        transmitBytes = transmitQueue.size();
    }
    log_info("Namco N2 card diagnostics: pipe=%s api=%s inserted=%s rx=%zu tx=%zu",
             linkState.load(std::memory_order_acquire) == LinkState::Connected
                 ? "connected" : "disconnected",
             apiConnected.load(std::memory_order_acquire) ? "connected" : "disconnected",
             cardInserted.load(std::memory_order_acquire) ? "yes" : "no",
             receiveBytes, transmitBytes);
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

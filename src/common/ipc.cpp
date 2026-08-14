#include "ipc.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "json.h"
#include "winutil.h"

#pragma comment(lib, "ws2_32.lib")

namespace cwut {
namespace {

const char* kProtocolTag = "CWUT1";

struct WinsockScope {
    bool ok = false;
    WinsockScope() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockScope() {
        if (ok) WSACleanup();
    }
};

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ' ') {
            if (!current.empty()) {
                fields.push_back(current);
                current.clear();
            }
        } else if (c != '\r' && c != '\n') {
            current.push_back(c);
        }
    }
    if (!current.empty()) fields.push_back(current);
    return fields;
}

bool parseLongLong(const std::string& text, long long& out) {
    if (text.empty() || text.size() > 20) return false;
    size_t index = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        index = 1;
        if (text.size() == 1) return false;
    }
    long long value = 0;
    for (; index < text.size(); ++index) {
        char c = text[index];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
        if (value < 0) return false;
    }
    out = negative ? -value : value;
    return true;
}

std::string fieldOrDash(bool present, long long value) {
    if (!present) return "-";
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld", value);
    return buf;
}

}  // namespace

std::wstring EndpointFilePath() {
    std::wstring dir = GetAppDataDir();
    if (dir.empty()) return std::wstring();
    return dir + L"\\endpoint.json";
}

bool WriteEndpointFile(const IpcEndpoint& endpoint, std::string* error) {
    const std::wstring path = EndpointFilePath();
    if (path.empty()) {
        if (error) *error = "cannot locate application data directory";
        return false;
    }
    JsonValue root = JsonValue::makeObject();
    root.set("version", JsonValue::makeNumber(1));
    root.set("port", JsonValue::makeNumber(endpoint.port));
    root.set("token", JsonValue::makeString(endpoint.token));
    root.set("pid", JsonValue::makeNumber(static_cast<double>(endpoint.pid)));
    return WriteAllBytesAtomic(path, JsonSerialize(root), /*userOnly=*/true, error);
}

bool ReadEndpointFile(IpcEndpoint& endpoint) {
    endpoint = IpcEndpoint();
    const std::wstring path = EndpointFilePath();
    if (path.empty()) return false;
    std::string text;
    if (!ReadAllBytes(path, text, 8192)) return false;
    JsonValue root;
    if (!JsonParse(text, root, nullptr)) return false;

    const JsonValue* port = root.find("port");
    const JsonValue* token = root.find("token");
    const JsonValue* pid = root.find("pid");
    if (port == nullptr || !port->isNumber()) return false;
    if (token == nullptr || !token->isString()) return false;
    if (port->number < 1 || port->number > 65535) return false;

    endpoint.port = static_cast<unsigned short>(port->number);
    endpoint.token = token->str;
    endpoint.pid = (pid != nullptr && pid->isNumber()) ? static_cast<unsigned long>(pid->number) : 0;
    return endpoint.valid();
}

void RemoveEndpointFile() {
    const std::wstring path = EndpointFilePath();
    if (!path.empty()) DeleteFileIfPresent(path);
}

std::string EncodeSnapshotMessage(const std::string& token, const UsageSnapshot& snapshot) {
    std::string line = kProtocolTag;
    line += ' ';
    line += token;
    line += ' ';
    line += fieldOrDash(snapshot.fiveHour.hasUsage, snapshot.fiveHour.usedPercent);
    line += ' ';
    line += fieldOrDash(snapshot.fiveHour.hasReset, snapshot.fiveHour.resetsAt);
    line += ' ';
    line += fieldOrDash(snapshot.sevenDay.hasUsage, snapshot.sevenDay.usedPercent);
    line += ' ';
    line += fieldOrDash(snapshot.sevenDay.hasReset, snapshot.sevenDay.resetsAt);
    line += '\n';
    return line;
}

bool DecodeSnapshotMessage(const std::string& line, const std::string& expectedToken,
                           UsageSnapshot& out) {
    out = UsageSnapshot();
    if (line.size() > kMaxMessageBytes) return false;
    std::vector<std::string> fields = splitFields(line);
    if (fields.size() != 6) return false;
    if (fields[0] != kProtocolTag) return false;
    if (expectedToken.empty()) return false;
    if (!ConstantTimeEquals(fields[1], expectedToken)) return false;

    UsageSnapshot parsed;
    auto readWindow = [](const std::string& usedText, const std::string& resetText,
                         RateWindow& window) -> bool {
        if (usedText != "-") {
            long long used = 0;
            if (!parseLongLong(usedText, used)) return false;
            int normalized = 0;
            if (!NormalizeUsedPercent(static_cast<double>(used), normalized)) return false;
            window.usedPercent = normalized;
            window.hasUsage = true;
        }
        if (resetText != "-") {
            long long reset = 0;
            if (!parseLongLong(resetText, reset)) return false;
            long long seconds = 0;
            if (NormalizeEpoch(static_cast<double>(reset), seconds)) {
                window.resetsAt = seconds;
                window.hasReset = true;
            }
        }
        return true;
    };

    if (!readWindow(fields[2], fields[3], parsed.fiveHour)) return false;
    if (!readWindow(fields[4], fields[5], parsed.sevenDay)) return false;
    if (!parsed.hasAnyUsage()) return false;

    parsed.receivedAtUnix = NowUnix();
    out = parsed;
    return true;
}

bool SendSnapshotToEndpoint(const IpcEndpoint& endpoint, const UsageSnapshot& snapshot,
                            int timeoutMs, std::string* error) {
    if (!endpoint.valid()) {
        if (error) *error = "no valid endpoint";
        return false;
    }
    WinsockScope winsock;
    if (!winsock.ok) {
        if (error) *error = "winsock unavailable";
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        if (error) *error = "socket failed";
        return false;
    }
    DWORD timeout = static_cast<DWORD>(timeoutMs);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Non-blocking connect so an unreachable tray cannot stall the status line.
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);
    int rc = connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (rc == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(sock);
            if (error) *error = "connect failed";
            return false;
        }
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(sock, &writable);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (select(0, nullptr, &writable, nullptr, &tv) <= 0) {
            closesocket(sock);
            if (error) *error = "connect timed out";
            return false;
        }
    }
    nonBlocking = 0;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    const std::string message = EncodeSnapshotMessage(endpoint.token, snapshot);
    size_t offset = 0;
    while (offset < message.size()) {
        int sent = send(sock, message.data() + offset, static_cast<int>(message.size() - offset), 0);
        if (sent <= 0) {
            closesocket(sock);
            if (error) *error = "send failed";
            return false;
        }
        offset += static_cast<size_t>(sent);
    }

    char reply[8] = {0};
    int got = recv(sock, reply, sizeof(reply) - 1, 0);
    closesocket(sock);
    if (got <= 0 || reply[0] != 'O') {
        if (error) *error = "tray did not acknowledge";
        return false;
    }
    return true;
}

bool SendSnapshotToTray(const UsageSnapshot& snapshot, int timeoutMs, std::string* error) {
    IpcEndpoint endpoint;
    if (!ReadEndpointFile(endpoint)) {
        if (error) *error = "tray is not running";
        return false;
    }
    return SendSnapshotToEndpoint(endpoint, snapshot, timeoutMs, error);
}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start(Handler handler, std::string* error) {
    static WinsockScope* winsock = new WinsockScope();
    if (!winsock->ok) {
        if (error) *error = "winsock unavailable";
        return false;
    }
    handler_ = std::move(handler);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        if (error) *error = "socket failed";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only, never 0.0.0.0
    address.sin_port = 0;                              // ephemeral
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(listener);
        if (error) *error = "bind failed";
        return false;
    }
    if (listen(listener, 8) == SOCKET_ERROR) {
        closesocket(listener);
        if (error) *error = "listen failed";
        return false;
    }
    int addressLength = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) ==
        SOCKET_ERROR) {
        closesocket(listener);
        if (error) *error = "getsockname failed";
        return false;
    }

    endpoint_.port = ntohs(address.sin_port);
    endpoint_.token = RandomHexToken(32);
    endpoint_.pid = GetCurrentProcessId();
    if (endpoint_.token.empty()) {
        closesocket(listener);
        if (error) *error = "random token generation failed";
        return false;
    }

    listenSocket_ = static_cast<unsigned long long>(listener);
    stopping_ = 0;
    thread_ = CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            static_cast<IpcServer*>(param)->acceptLoop();
            return 0;
        },
        this, 0, nullptr);
    if (thread_ == nullptr) {
        closesocket(listener);
        listenSocket_ = ~0ull;
        if (error) *error = "cannot start listener thread";
        return false;
    }
    return true;
}

void IpcServer::stop() {
    if (listenSocket_ == ~0ull) return;
    InterlockedExchange(&stopping_, 1);
    closesocket(static_cast<SOCKET>(listenSocket_));
    listenSocket_ = ~0ull;
    if (thread_ != nullptr) {
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
}

void IpcServer::acceptLoop() {
    for (;;) {
        sockaddr_in peer{};
        int peerLength = sizeof(peer);
        SOCKET client = accept(static_cast<SOCKET>(listenSocket_),
                               reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (client == INVALID_SOCKET) {
            if (stopping_ != 0) return;
            continue;
        }
        // Defence in depth: the socket is bound to loopback, so this should
        // always hold, but reject anything else outright.
        if (peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            closesocket(client);
            continue;
        }

        DWORD timeout = 2000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                   sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                   sizeof(timeout));

        std::string buffer;
        bool overflow = false;
        for (;;) {
            char chunk[128];
            int got = recv(client, chunk, sizeof(chunk), 0);
            if (got <= 0) break;
            buffer.append(chunk, static_cast<size_t>(got));
            if (buffer.size() > kMaxMessageBytes) {
                overflow = true;
                break;
            }
            if (buffer.find('\n') != std::string::npos) break;
        }

        if (!overflow) {
            size_t end = buffer.find('\n');
            const std::string line = (end == std::string::npos) ? buffer : buffer.substr(0, end);
            UsageSnapshot snapshot;
            if (DecodeSnapshotMessage(line, endpoint_.token, snapshot)) {
                send(client, "OK\n", 3, 0);
                if (handler_) handler_(snapshot);
            } else {
                send(client, "NO\n", 3, 0);
            }
        }
        closesocket(client);
        if (stopping_ != 0) return;
    }
}

}  // namespace cwut

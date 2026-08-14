// Local-only transport between the status-line helper and the tray process.
//
// The channel carries one line of six ASCII fields and nothing else. It binds
// to 127.0.0.1 on an ephemeral port and requires a 256-bit token that lives in
// a file readable only by the current user account, so another account on the
// machine cannot push usage into the tray.
#pragma once

#include <functional>
#include <string>

#include "usage.h"

namespace cwut {

struct IpcEndpoint {
    unsigned short port = 0;
    std::string token;
    unsigned long pid = 0;

    bool valid() const { return port != 0 && token.size() >= 32; }
};

std::wstring EndpointFilePath();
bool WriteEndpointFile(const IpcEndpoint& endpoint, std::string* error);
bool ReadEndpointFile(IpcEndpoint& endpoint);
void RemoveEndpointFile();

// "CWUT1 <token> <fiveUsed> <fiveReset> <sevenUsed> <sevenReset>\n", with "-"
// standing in for any field the payload did not supply.
std::string EncodeSnapshotMessage(const std::string& token, const UsageSnapshot& snapshot);

// Returns false on a malformed line or a token mismatch. The token comparison
// does not exit early.
bool DecodeSnapshotMessage(const std::string& line, const std::string& expectedToken,
                           UsageSnapshot& out);

// Best-effort send. Never blocks longer than timeoutMs, because this runs
// inside Claude Code's status-line command.
bool SendSnapshotToEndpoint(const IpcEndpoint& endpoint, const UsageSnapshot& snapshot,
                            int timeoutMs, std::string* error);
bool SendSnapshotToTray(const UsageSnapshot& snapshot, int timeoutMs, std::string* error);

constexpr int kDefaultSendTimeoutMs = 700;
constexpr size_t kMaxMessageBytes = 512;

class IpcServer {
public:
    using Handler = std::function<void(const UsageSnapshot&)>;

    IpcServer() = default;
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start(Handler handler, std::string* error);
    void stop();

    unsigned short port() const { return endpoint_.port; }
    const std::string& token() const { return endpoint_.token; }
    const IpcEndpoint& endpoint() const { return endpoint_; }

private:
    void acceptLoop();

    IpcEndpoint endpoint_;
    Handler handler_;
    unsigned long long listenSocket_ = ~0ull;
    void* thread_ = nullptr;
    volatile long stopping_ = 0;
};

}  // namespace cwut

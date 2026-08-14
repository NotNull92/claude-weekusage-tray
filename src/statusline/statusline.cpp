// Status-line mode.
//
// Claude Code runs `ClaudeWeekUsageTray.exe --statusline` as its statusLine
// command and pipes a JSON payload to it on stdin. This code picks four
// numbers out of that payload, sends them to a running tray over loopback, and
// prints a status line. It never inspects, stores, or forwards anything else.
#include "statusline.h"

#include <windows.h>

#include <string>
#include <vector>

#include "../common/ipc.h"
#include "../common/json.h"
#include "../common/usage.h"
#include "../common/winutil.h"

namespace cwut {
namespace {

constexpr size_t kMaxStdinBytes = 1u * 1024u * 1024u;

std::string readAllStdin() {
    std::string data;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) return data;
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        data.append(buffer, read);
        if (data.size() > kMaxStdinBytes) {
            data.resize(kMaxStdinBytes);
            break;
        }
    }
    return data;
}

void writeStdout(const std::string& text) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) return;
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!WriteFile(output, text.data() + offset, static_cast<DWORD>(text.size() - offset),
                       &written, nullptr) ||
            written == 0) {
            return;
        }
        offset += written;
    }
}

std::string wrappedCommand() {
    std::wstring dir = GetAppDataDir();
    if (dir.empty()) return std::string();
    std::string text;
    if (!ReadAllBytes(dir + L"\\wrapped-statusline.json", text, 65536)) return std::string();
    JsonValue root;
    if (!JsonParse(text, root, nullptr)) return std::string();
    const JsonValue* command = root.find("wrapped_command");
    if (command == nullptr || !command->isString()) return std::string();
    return command->str;
}

// Runs the user's original status-line command with the same stdin and returns
// its stdout. Returns false if the command could not be started.
bool runWrapped(const std::string& command, const std::string& input, std::string& output) {
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;

    HANDLE childStdinRead = nullptr, childStdinWrite = nullptr;
    HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
    if (!CreatePipe(&childStdinRead, &childStdinWrite, &inherit, 0)) return false;
    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &inherit, 0)) {
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWrite);
        return false;
    }
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    wchar_t comspec[MAX_PATH];
    if (GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH) == 0) {
        wcscpy_s(comspec, L"cmd.exe");
    }
    std::wstring line = L"\"" + std::wstring(comspec) + L"\" /c " + ToWide(command);
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = childStdinRead;
    startup.hStdOutput = childStdoutWrite;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    if (!started) {
        CloseHandle(childStdinWrite);
        CloseHandle(childStdoutRead);
        return false;
    }

    size_t offset = 0;
    while (offset < input.size()) {
        DWORD written = 0;
        if (!WriteFile(childStdinWrite, input.data() + offset,
                       static_cast<DWORD>(input.size() - offset), &written, nullptr) ||
            written == 0) {
            break;
        }
        offset += written;
    }
    CloseHandle(childStdinWrite);

    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(childStdoutRead, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        output.append(buffer, read);
        if (output.size() > kMaxStdinBytes) break;
    }
    CloseHandle(childStdoutRead);

    WaitForSingleObject(process.hProcess, 10000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::string ownStatusLine(const UsageSnapshot& snapshot, bool valid) {
    if (!valid) return std::string();
    std::string text = "Claude";
    if (snapshot.fiveHour.hasUsage) {
        text += "  5h " + std::to_string(snapshot.fiveHour.remainingPercent()) + "% left";
    }
    if (snapshot.sevenDay.hasUsage) {
        text += "  7d " + std::to_string(snapshot.sevenDay.remainingPercent()) + "% left";
    }
    return text;
}

}  // namespace

int RunStatusLine() {
    const std::string input = readAllStdin();

    UsageSnapshot snapshot;
    const bool valid = ParseStatusLinePayload(input, snapshot, nullptr);
    if (valid) {
        // Best effort. A missing tray must never slow down or break the status
        // line, so failures here are silent.
        SendSnapshotToTray(snapshot, kDefaultSendTimeoutMs, nullptr);
    }

    const std::string wrapped = wrappedCommand();
    if (!wrapped.empty()) {
        std::string output;
        if (runWrapped(wrapped, input, output)) {
            writeStdout(output);
            return 0;
        }
        // Fall through to our own line if the wrapped command could not run.
    }

    const std::string line = ownStatusLine(snapshot, valid);
    if (!line.empty()) writeStdout(line + "\n");
    return 0;
}

}  // namespace cwut

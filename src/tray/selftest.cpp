// Focused tests for the parser, the conversion rules, the IPC channel, the
// tray glyph, the panel, and the no-token boundary.
//
// Run with:  ClaudeWeekUsageTray.exe --self-test
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "../common/ipc.h"
#include "../common/json.h"
#include "../common/usage.h"
#include "../common/winutil.h"
#include "cli.h"
#include "menu.h"
#include "panel.h"
#include "trayicon.h"

namespace cwut {
namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        ++g_passed;
        Out("  pass  " + name);
    } else {
        ++g_failed;
        Out("  FAIL  " + name);
    }
}

std::string payload(const std::string& body) { return "{\"rate_limits\":" + body + "}"; }

std::wstring baseNameOf(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

// The definition-of-done payload.
const char* kReferencePayload =
    "{\"session_id\":\"ignored\",\"model\":{\"display_name\":\"ignored\"},"
    "\"rate_limits\":{\"five_hour\":{\"used_percentage\":27,\"resets_at\":1780000000},"
    "\"seven_day\":{\"used_percentage\":41,\"resets_at\":1780400000}}}";

void testJson() {
    Out("JSON reader");
    JsonValue value;
    check(JsonParse("{\"a\":[1,2,{\"b\":null}],\"c\":\"x\\u00e9\"}", value, nullptr),
          "parses nested document");
    check(value.find("a") != nullptr && value.find("a")->isArray() &&
              value.find("a")->array.size() == 3,
          "keeps array shape");
    check(value.find("c") != nullptr && value.find("c")->str == "x\xc3\xa9",
          "decodes \\u escape to UTF-8");

    check(!JsonParse("{\"a\":1,}", value, nullptr), "rejects trailing comma");
    check(!JsonParse("{'a':1}", value, nullptr), "rejects single quotes");
    check(!JsonParse("{\"a\":01}", value, nullptr), "rejects leading zero number");
    check(!JsonParse("", value, nullptr), "rejects empty input");
    check(!JsonParse("{\"a\":", value, nullptr), "rejects truncated input");

    std::string deep;
    for (int i = 0; i < 200; ++i) deep += "[";
    check(!JsonParse(deep, value, nullptr), "rejects excessive nesting");

    JsonValue round;
    check(JsonParse("{\"keep\":{\"n\":1.5},\"other\":true}", round, nullptr) &&
              JsonSerialize(round).find("\"keep\"") != std::string::npos &&
              JsonSerialize(round).find("1.5") != std::string::npos,
          "round-trips values it does not own");
}

void testConversion() {
    Out("Usage conversion");
    UsageSnapshot snapshot;
    check(ParseStatusLinePayload(kReferencePayload, snapshot, nullptr), "reads reference payload");
    snapshot.receivedAtUnix = NowUnix();
    check(snapshot.fiveHour.hasUsage && snapshot.fiveHour.usedPercent == 27,
          "five-hour used is 27");
    check(snapshot.fiveHour.remainingPercent() == 73, "five-hour remaining is 73");
    check(snapshot.sevenDay.remainingPercent() == 59, "seven-day remaining is 59");
    check(FormatTrayLabel(snapshot) == "73", "tray label is 73");
    check(snapshot.fiveHour.hasReset && snapshot.fiveHour.resetsAt == 1780000000LL,
          "five-hour reset time accepted");
    check(snapshot.sevenDay.hasReset && snapshot.sevenDay.resetsAt == 1780400000LL,
          "seven-day reset time accepted");

    int used = 0;
    check(NormalizeUsedPercent(-5.0, used) && used == 0, "clamps negative to 0");
    check(NormalizeUsedPercent(140.0, used) && used == 100, "clamps above 100 to 100");
    check(NormalizeUsedPercent(27.2, used) && used == 28, "rounds fractional usage up");
    check(NormalizeUsedPercent(0.0, used) && used == 0, "keeps 0 as 0");
    check(NormalizeUsedPercent(100.0, used) && used == 100, "keeps 100 as 100");

    long long seconds = 0;
    check(NormalizeEpoch(1780000000.0, seconds) && seconds == 1780000000LL, "accepts valid epoch");
    check(NormalizeEpoch(1780000000000.0, seconds) && seconds == 1780000000LL,
          "accepts millisecond epoch");
    check(!NormalizeEpoch(0.0, seconds), "rejects zero epoch");
    check(!NormalizeEpoch(-1.0, seconds), "rejects negative epoch");
    check(!NormalizeEpoch(1.0e18, seconds), "rejects absurd epoch");
    check(!NormalizeEpoch(946684800.0, seconds), "rejects epoch before 2020");
}

void testFailClosed() {
    Out("Invalid input fails closed");
    UsageSnapshot snapshot;
    const char* bad[] = {
        "",
        "not json",
        "{}",
        "{\"rate_limits\":null}",
        "{\"rate_limits\":{}}",
        "{\"rate_limits\":{\"five_hour\":{}}}",
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":\"27\"}}}",
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":true}}}",
        "{\"rate_limits\":[27,41]}",
    };
    bool allRejected = true;
    for (const char* text : bad) {
        UsageSnapshot parsed;
        if (ParseStatusLinePayload(text, parsed, nullptr)) allRejected = false;
        if (parsed.hasAnyUsage()) allRejected = false;
    }
    check(allRejected, "rejects malformed and partial payloads");

    UsageSnapshot empty;
    check(FormatTrayLabel(empty) == "--", "tray shows -- with no snapshot");

    UsageSnapshot resetOnly;
    check(!ParseStatusLinePayload(payload("{\"five_hour\":{\"resets_at\":1780000000}}"), resetOnly,
                                  nullptr),
          "a reset time alone is not usable data");
    check(FormatTrayLabel(resetOnly) == "--", "tray still shows -- for reset-only payload");

    // A seven-day-only payload is usable data, but the tray glyph tracks the
    // five-hour window and must not borrow the other number.
    UsageSnapshot sevenOnly;
    check(ParseStatusLinePayload(payload("{\"seven_day\":{\"used_percentage\":41}}"), sevenOnly,
                                 nullptr),
          "accepts a seven-day-only payload");
    sevenOnly.receivedAtUnix = NowUnix();
    check(FormatTrayLabel(sevenOnly) == "--", "tray shows -- when five-hour usage is missing");

    UsageSnapshot badReset;
    check(ParseStatusLinePayload(
              payload("{\"five_hour\":{\"used_percentage\":27,\"resets_at\":42}}"), badReset,
              nullptr),
          "keeps usage when only the reset time is invalid");
    check(!badReset.fiveHour.hasReset, "invalid reset time is dropped");
    check(FormatResetTime(badReset.fiveHour, NowUnix()) == "Unavailable",
          "invalid reset time renders as Unavailable");
}

void testLabels() {
    Out("Tray label and staleness");
    UsageSnapshot snapshot;
    snapshot.receivedAtUnix = NowUnix();
    snapshot.fiveHour.hasUsage = true;

    snapshot.fiveHour.usedPercent = 27;
    check(FormatTrayLabel(snapshot) == "73", "two-digit label 73");
    snapshot.fiveHour.usedPercent = 5;
    check(FormatTrayLabel(snapshot) == "95", "two-digit label 95");
    snapshot.fiveHour.usedPercent = 0;
    check(FormatTrayLabel(snapshot) == "100", "full label 100");
    snapshot.fiveHour.usedPercent = 100;
    check(FormatTrayLabel(snapshot) == "0", "label 0 when the window is spent");

    const long long now = NowUnix();
    snapshot.receivedAtUnix = now;
    check(!IsStale(snapshot, now), "fresh snapshot is not stale");
    check(!IsStale(snapshot, now + kStaleAfterSeconds - 1), "still fresh just inside the window");
    check(IsStale(snapshot, now + kStaleAfterSeconds + 1), "stale past the window");

    UsageSnapshot never;
    check(IsStale(never, now), "a snapshot that never arrived counts as stale");
    check(FormatRelativeAge(never.receivedAtUnix, now) == "never", "age reads never");
    check(FormatRelativeAge(now - 120, now) == "2m ago", "age reads 2m ago");
}

void testIcon() {
    Out("Tray glyph");
    IconStyle style;
    style.sizePixels = 16;

    auto visiblePixels = [](const std::wstring& label, const IconStyle& s) -> int {
        HICON icon = CreateLabelIcon(label, s);
        if (icon == nullptr) return -1;
        ICONINFO info{};
        if (!GetIconInfo(icon, &info)) {
            DestroyIcon(icon);
            return -1;
        }
        BITMAPINFO header{};
        header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        header.bmiHeader.biWidth = s.sizePixels;
        header.bmiHeader.biHeight = -s.sizePixels;
        header.bmiHeader.biPlanes = 1;
        header.bmiHeader.biBitCount = 32;
        header.bmiHeader.biCompression = BI_RGB;
        std::vector<unsigned char> pixels(static_cast<size_t>(s.sizePixels) * s.sizePixels * 4);
        HDC dc = GetDC(nullptr);
        int lines = GetDIBits(dc, info.hbmColor, 0, static_cast<UINT>(s.sizePixels), pixels.data(),
                              &header, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        if (info.hbmColor != nullptr) DeleteObject(info.hbmColor);
        if (info.hbmMask != nullptr) DeleteObject(info.hbmMask);
        DestroyIcon(icon);
        if (lines == 0) return -1;
        int count = 0;
        for (size_t i = 3; i < pixels.size(); i += 4) {
            if (pixels[i] != 0) ++count;
        }
        return count;
    };

    const int two = visiblePixels(L"73", style);
    const int three = visiblePixels(L"100", style);
    const int dashes = visiblePixels(L"--", style);
    check(two > 0, "renders a two-digit label");
    check(three > 0, "renders a three-digit label");
    check(dashes > 0, "renders the -- placeholder");
    check(two > dashes, "two digits cover more pixels than the placeholder");

    IconStyle dim = style;
    dim.dimmed = true;
    HICON dimmed = CreateLabelIcon(L"73", dim);
    check(dimmed != nullptr, "renders a dimmed stale glyph");
    if (dimmed != nullptr) DestroyIcon(dimmed);

    IconStyle large = style;
    large.sizePixels = 32;
    HICON big = CreateLabelIcon(L"100", large);
    check(big != nullptr, "renders at 32 pixels for high-DPI trays");
    if (big != nullptr) DestroyIcon(big);
}

void testMenuAndPanel() {
    Out("Menu and panel");
    HMENU menu = BuildTrayMenu();
    check(menu != nullptr && GetMenuItemCount(menu) == 2, "menu has exactly two commands");
    if (menu != nullptr) {
        wchar_t text[64] = {0};
        GetMenuStringW(menu, kCommandShowPanel, text, 63, MF_BYCOMMAND);
        check(std::wstring(text) == L"Show panel", "menu offers Show panel");
        GetMenuStringW(menu, kCommandExit, text, 63, MF_BYCOMMAND);
        check(std::wstring(text) == L"Exit", "menu offers Exit");
        DestroyMenu(menu);
    }

    DetailPanel panel;
    check(panel.create(GetModuleHandleW(nullptr)), "panel window is created");
    check(!panel.visible(), "panel starts hidden");

    UsageSnapshot snapshot;
    ParseStatusLinePayload(kReferencePayload, snapshot, nullptr);
    snapshot.receivedAtUnix = NowUnix();
    panel.setSnapshot(snapshot);

    RECT anchor{1000, 1000, 1016, 1016};
    panel.toggle(anchor);
    check(panel.visible(), "left-click style toggle shows the panel");
    RECT first{};
    GetWindowRect(panel.hwnd(), &first);
    UsageSnapshot later = snapshot;
    later.receivedAtUnix = NowUnix();
    panel.setSnapshot(later);
    RECT second{};
    GetWindowRect(panel.hwnd(), &second);
    check(first.left == second.left && first.top == second.top,
          "an incoming update leaves the open panel where it is");

    panel.toggle(anchor);
    check(!panel.visible(), "a second toggle hides it");
    SendMessageW(panel.hwnd(), WM_CLOSE, 0, 0);
    check(!panel.visible() && IsWindow(panel.hwnd()), "close hides the panel without ending the app");
    panel.destroy();
}

void testIpc() {
    Out("Local IPC channel");
    IpcServer server;
    UsageSnapshot received;
    HANDLE arrived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::string error;
    bool started = server.start(
        [&](const UsageSnapshot& snapshot) {
            received = snapshot;
            SetEvent(arrived);
        },
        &error);
    check(started, "server binds a loopback port");
    if (!started) {
        CloseHandle(arrived);
        return;
    }
    check(server.token().size() == 64, "server generates a 256-bit token");

    UsageSnapshot outgoing;
    ParseStatusLinePayload(kReferencePayload, outgoing, nullptr);
    check(SendSnapshotToEndpoint(server.endpoint(), outgoing, 2000, &error),
          "authenticated send is accepted");
    check(WaitForSingleObject(arrived, 3000) == WAIT_OBJECT_0, "server delivers the snapshot");
    check(received.fiveHour.usedPercent == 27 && received.sevenDay.usedPercent == 41,
          "both windows survive the round trip");
    check(received.fiveHour.resetsAt == 1780000000LL && received.sevenDay.resetsAt == 1780400000LL,
          "both reset times survive the round trip");
    check(received.receivedAtUnix != 0, "receipt time is stamped by the tray, not the sender");

    IpcEndpoint forged = server.endpoint();
    forged.token = std::string(64, 'a');
    check(!SendSnapshotToEndpoint(forged, outgoing, 2000, &error),
          "a wrong token is refused");

    const std::string good = EncodeSnapshotMessage(server.token(), outgoing);
    UsageSnapshot decoded;
    check(DecodeSnapshotMessage(good, server.token(), decoded), "well-formed message decodes");
    check(!DecodeSnapshotMessage(good, std::string(64, 'b'), decoded),
          "decode refuses a mismatched token");
    check(!DecodeSnapshotMessage("CWUT1 " + server.token() + " 27 1780000000\n", server.token(),
                                 decoded),
          "decode refuses a short message");
    check(!DecodeSnapshotMessage("OTHER " + server.token() + " 27 - 41 -\n", server.token(),
                                 decoded),
          "decode refuses an unknown protocol tag");
    check(!DecodeSnapshotMessage(std::string(kMaxMessageBytes + 10, 'x'), server.token(), decoded),
          "decode refuses an oversized message");
    check(!DecodeSnapshotMessage("CWUT1 " + server.token() + " - - - -\n", server.token(), decoded),
          "decode refuses a message with no usable window");
    check(DecodeSnapshotMessage("CWUT1 " + server.token() + " 999 - - -\n", server.token(),
                                decoded) &&
              decoded.fiveHour.usedPercent == 100,
          "decode clamps an out-of-range percentage");

    check(good.find(server.token()) != std::string::npos, "the wire carries the token");
    check(good.find("session") == std::string::npos && good.find("model") == std::string::npos,
          "the wire carries nothing from the rest of the payload");

    server.stop();
    CloseHandle(arrived);
}

// Runs this executable in status-line mode the way Claude Code does: payload
// in on a stdin pipe, status line out on a stdout pipe. A GUI-subsystem
// program keeps both handles when they are redirected, and this proves it.
void testStatusLineMode() {
    Out("Status-line mode");

    // Point the child at an empty application-data folder so it finds no
    // endpoint file and cannot disturb a tray the user has running.
    wchar_t previousLocalAppData[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData, MAX_PATH);
    wchar_t sandbox[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, sandbox);
    wcscat_s(sandbox, L"cwut-selftest");
    CreateDirectoryW(sandbox, nullptr);
    SetEnvironmentVariableW(L"LOCALAPPDATA", sandbox);

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    bool piped = CreatePipe(&inRead, &inWrite, &inherit, 0) != 0 &&
                 CreatePipe(&outRead, &outWrite, &inherit, 0) != 0;
    check(piped, "test pipes are created");
    if (piped) {
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

        std::wstring command = L"\"" + GetExecutablePath() + L"\" --statusline";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = inRead;
        startup.hStdOutput = outWrite;
        startup.hStdError = outWrite;
        PROCESS_INFORMATION process{};
        BOOL started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, 0,
                                      nullptr, nullptr, &startup, &process);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        check(started != 0, "status-line mode starts");

        std::string output;
        if (started) {
            const std::string payload(kReferencePayload);
            DWORD written = 0;
            WriteFile(inWrite, payload.data(), static_cast<DWORD>(payload.size()), &written,
                      nullptr);
            CloseHandle(inWrite);
            char buffer[512];
            for (;;) {
                DWORD read = 0;
                if (!ReadFile(outRead, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
                output.append(buffer, read);
                if (output.size() > 4096) break;
            }
            WaitForSingleObject(process.hProcess, 15000);
            DWORD exitCode = 1;
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            check(exitCode == 0, "status-line mode exits cleanly");
        } else {
            CloseHandle(inWrite);
        }
        CloseHandle(outRead);

        check(output.find("5h 73% left") != std::string::npos,
              "status line reports 73% of the five-hour window left");
        check(output.find("7d 59% left") != std::string::npos,
              "status line reports 59% of the seven-day window left");
        check(output.find("session") == std::string::npos,
              "status line echoes nothing else from the payload");
    }

    SetEnvironmentVariableW(L"LOCALAPPDATA",
                            previousLocalAppData[0] != L'\0' ? previousLocalAppData : nullptr);
    RemoveDirectoryW(sandbox);
}

// Reads the shipped binary back off disk and proves the strings a
// credential-reading build would need are simply not in them. The needles are
// stored reversed so this test does not put them in the binary itself.
void testNoTokenBoundary() {
    Out("No-token boundary");
    const char* reversedNeedles[] = {
        "nosj.slaitnederc.",       // .credentials.json
        "moc.ciporhtna.ipa",       // api.anthropic.com
        "egasu/htuao/ipa",         // api/oauth/usage
        "yeK-IPA-x",               // x-api-Key
        "reraeB",                  // Bearer
        "YEK_IPA_CIPORHTNA",       // ANTHROPIC_API_KEY
        "WdaeRderC",               // CredReadW
        "WetaremunEderC",          // CredEnumerateW
    };

    std::vector<std::wstring> binaries;
    binaries.push_back(GetExecutablePath());

    bool clean = true;
    for (const std::wstring& binary : binaries) {
        std::string bytes;
        if (!ReadAllBytes(binary, bytes, 64u * 1024u * 1024u)) {
            check(false, "can read " + ToUtf8(baseNameOf(binary)) + " for scanning");
            clean = false;
            continue;
        }
        std::string lower = bytes;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        for (const char* reversed : reversedNeedles) {
            std::string needle(reversed);
            std::reverse(needle.begin(), needle.end());
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            // UTF-16LE form as well, since Windows APIs take wide strings.
            std::string wide;
            for (char c : needle) {
                wide.push_back(c);
                wide.push_back('\0');
            }
            if (lower.find(needle) != std::string::npos || lower.find(wide) != std::string::npos) {
                check(false, "binary is free of \"" + needle + "\"");
                clean = false;
            }
        }
    }
    check(clean, "the shipped binary contains no credential or token strings");

    // The data model has nowhere to put a secret even if one were handed over.
    check(sizeof(UsageSnapshot) <= 64, "the snapshot holds only the four scalars and a timestamp");
}

}  // namespace

int RunSelfTest() {
    Out("ClaudeWeekUsageTray self-test");
    Out("");
    testJson();
    testConversion();
    testFailClosed();
    testLabels();
    testIcon();
    testMenuAndPanel();
    testIpc();
    testStatusLineMode();
    testNoTokenBoundary();
    Out("");
    Out(std::to_string(g_passed) + " passed, " + std::to_string(g_failed) + " failed");
    return g_failed == 0 ? 0 : 1;
}

}  // namespace cwut

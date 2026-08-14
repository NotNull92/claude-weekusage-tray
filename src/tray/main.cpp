// ClaudeWeekUsageTray - shows the remaining Claude Code 5-hour limit in the
// Windows notification area.
//
// The only usage data this process ever holds is the four scalars Claude Code
// hands to its status-line command. It reads no credential store and talks to
// no network service.
#include <windows.h>

#include <shellapi.h>

#include <string>

#include "../common/ipc.h"
#include "../common/usage.h"
#include "../common/winutil.h"
#include "../statusline/statusline.h"
#include "cli.h"
#include "menu.h"
#include "panel.h"
#include "trayicon.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {
namespace {

const wchar_t* kVersion = L"1.0.4";
const wchar_t* kWindowClass = kTrayWindowClass;
const wchar_t* kMutexName = L"Local\\ClaudeWeekUsageTray.SingleInstance";

constexpr UINT WM_TRAY_CALLBACK = WM_APP + 1;
constexpr UINT WM_SNAPSHOT_ARRIVED = WM_APP + 2;
constexpr UINT_PTR kStaleTimerId = 1;
constexpr UINT kStaleTimerMs = 30 * 1000;
constexpr UINT kTrayIconId = 1;

class TrayApp {
public:
    int run(HINSTANCE instance);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void addIcon();
    void refreshIcon();
    void removeIcon();
    RECT iconRect() const;
    void showMenu();
    std::wstring tooltipText() const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    DetailPanel panel_;
    IpcServer server_;
    UINT taskbarCreatedMessage_ = 0;

    CRITICAL_SECTION lock_{};
    UsageSnapshot snapshot_;  // guarded by lock_
    UsageSnapshot uiSnapshot_;
};

TrayApp* g_app = nullptr;

void enableDpiAwareness() {
    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;
    SetContextFn setContext =
        reinterpret_cast<SetContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext != nullptr) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (setContext(reinterpret_cast<HANDLE>(-4))) return;
    }
    SetProcessDPIAware();
}

RECT fallbackAnchor() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT anchor{work.right - 24, work.bottom - 24, work.right - 8, work.bottom - 8};
    return anchor;
}

LRESULT TrayApp::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        // Explorer restarted; put the icon back.
        addIcon();
        return 0;
    }

    switch (message) {
        case WM_TRAY_CALLBACK: {
            const UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
                panel_.setSnapshot(uiSnapshot_);
                panel_.toggle(iconRect());
            } else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                showMenu();
            }
            return 0;
        }
        case WM_SNAPSHOT_ARRIVED: {
            EnterCriticalSection(&lock_);
            uiSnapshot_ = snapshot_;
            LeaveCriticalSection(&lock_);
            refreshIcon();
            if (panel_.visible()) panel_.setSnapshot(uiSnapshot_);
            return 0;
        }
        case WM_TIMER:
            if (wParam == kStaleTimerId) {
                // Nothing is fetched here. This only re-renders local state so
                // the glyph dims and the panel wording turns stale on time.
                refreshIcon();
                if (panel_.visible()) panel_.setSnapshot(uiSnapshot_);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCommandShowPanel:
                    panel_.setSnapshot(uiSnapshot_);
                    panel_.show(iconRect());
                    return 0;
                case kCommandExit:
                    DestroyWindow(hwnd);
                    return 0;
                default: break;
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_DPICHANGED:
            refreshIcon();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kStaleTimerId);
            removeIcon();
            panel_.destroy();
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK TrayApp::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_app != nullptr) return g_app->handle(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::wstring TrayApp::tooltipText() const {
    const long long now = NowUnix();
    if (uiSnapshot_.receivedAtUnix == 0) {
        return L"Claude usage: no data yet\nStart Claude Code with the status line configured.";
    }
    std::wstring text = L"Claude";
    text += L"\n5-hour limit: ";
    text += uiSnapshot_.fiveHour.hasUsage
                ? std::to_wstring(uiSnapshot_.fiveHour.remainingPercent()) + L"% remaining"
                : L"unavailable";
    text += L"\n7-day limit: ";
    text += uiSnapshot_.sevenDay.hasUsage
                ? std::to_wstring(uiSnapshot_.sevenDay.remainingPercent()) + L"% remaining"
                : L"unavailable";
    text += L"\nLast update: " + ToWide(FormatRelativeAge(uiSnapshot_.receivedAtUnix, now));
    if (IsStale(uiSnapshot_, now)) text += L" (stale)";
    return text;
}

void TrayApp::addIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_TRAY_CALLBACK;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tooltipText().c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    refreshIcon();
}

void TrayApp::refreshIcon() {
    IconStyle style;
    int size = GetSystemMetrics(SM_CXSMICON);
    style.sizePixels = size > 0 ? size : 16;
    style.lightTheme = SystemUsesLightTheme();
    // Dimming says "this number is old". With no number at all there is
    // nothing to distrust, so "--" is drawn at full strength.
    style.dimmed = uiSnapshot_.receivedAtUnix != 0 && IsStale(uiSnapshot_, NowUnix());

    HICON fresh = CreateLabelIcon(ToWide(FormatTrayLabel(uiSnapshot_)), style);
    if (fresh == nullptr) return;

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.hIcon = fresh;
    wcsncpy_s(data.szTip, tooltipText().c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);

    if (icon_ != nullptr) DestroyIcon(icon_);
    icon_ = fresh;
}

void TrayApp::removeIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    if (icon_ != nullptr) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

RECT TrayApp::iconRect() const {
    using GetRectFn = HRESULT(WINAPI*)(NOTIFYICONIDENTIFIER*, RECT*);
    static GetRectFn getRect = reinterpret_cast<GetRectFn>(
        GetProcAddress(GetModuleHandleW(L"shell32.dll"), "Shell_NotifyIconGetRect"));
    if (getRect != nullptr) {
        NOTIFYICONIDENTIFIER id{};
        id.cbSize = sizeof(id);
        id.hWnd = hwnd_;
        id.uID = kTrayIconId;
        RECT rect{};
        if (SUCCEEDED(getRect(&id, &rect))) return rect;
    }
    return fallbackAnchor();
}

void TrayApp::showMenu() {
    HMENU menu = BuildTrayMenu();
    if (menu == nullptr) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

int TrayApp::run(HINSTANCE instance) {
    instance_ = instance;
    g_app = this;
    InitializeCriticalSection(&lock_);

    HANDLE single = CreateMutexW(nullptr, TRUE, kMutexName);
    if (single != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Starting the program a second time opens the panel of the copy that
        // is already running rather than complaining about it.
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing != nullptr) {
            PostMessageW(existing, WM_COMMAND, kCommandShowPanel, 0);
        } else {
            MessageBoxW(nullptr, L"ClaudeWeekUsageTray is already running.", L"ClaudeWeekUsageTray",
                        MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(single);
        return 0;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayApp::windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // A hidden top-level window rather than a message-only one, so a second
    // launch can find it and ask for the panel.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"ClaudeWeekUsageTray", WS_POPUP, 0, 0,
                            0, 0, nullptr, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) return 1;

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    panel_.create(instance);

    std::string error;
    if (!server_.start(
            [this](const UsageSnapshot& snapshot) {
                EnterCriticalSection(&lock_);
                snapshot_ = snapshot;
                LeaveCriticalSection(&lock_);
                PostMessageW(hwnd_, WM_SNAPSHOT_ARRIVED, 0, 0);
            },
            &error)) {
        MessageBoxW(nullptr,
                    (L"Could not open the local update channel: " + ToWide(error)).c_str(),
                    L"ClaudeWeekUsageTray", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!WriteEndpointFile(server_.endpoint(), &error)) {
        MessageBoxW(nullptr, (L"Could not publish the local endpoint: " + ToWide(error)).c_str(),
                    L"ClaudeWeekUsageTray", MB_OK | MB_ICONERROR);
        return 1;
    }

    addIcon();
    // Without the status-line setting the tray can only ever show "--", so say
    // so on startup rather than sitting there looking broken.
    OfferStatusLineSetup();
    SetTimer(hwnd_, kStaleTimerId, kStaleTimerMs, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    server_.stop();
    RemoveEndpointFile();
    DeleteCriticalSection(&lock_);
    if (single != nullptr) {
        ReleaseMutex(single);
        CloseHandle(single);
    }
    g_app = nullptr;
    return static_cast<int>(message.wParam);
}

void printUsage() {
    Out("ClaudeWeekUsageTray " + ToUtf8(std::wstring(kVersion)));
    Out("Shows the remaining Claude Code 5-hour limit in the Windows notification area.");
    Out("");
    Out("Usage:");
    Out("  ClaudeWeekUsageTray.exe                    Start the tray icon.");
    Out("  ClaudeWeekUsageTray.exe --setup            Point Claude Code's status line at this");
    Out("                                             program. Refuses to replace an existing");
    Out("                                             command unless --wrap-existing is given.");
    Out("  ClaudeWeekUsageTray.exe --setup --wrap-existing");
    Out("                                             Keep the existing status-line command and");
    Out("                                             run it as well.");
    Out("  ClaudeWeekUsageTray.exe --remove-statusline");
    Out("                                             Undo the setup and restore what was there.");
    Out("  ClaudeWeekUsageTray.exe --cleanup-tray-icons [--apply]");
    Out("                                             Report, and with --apply remove, leftover");
    Out("                                             notification-area entries for this program");
    Out("                                             in your own account. Writes a .reg backup");
    Out("                                             first and never deletes files.");
    Out("  ClaudeWeekUsageTray.exe --uninstall        Stop the icon, undo the status line, and");
    Out("                                             clear the notification-area entries. This");
    Out("                                             is what uninstall.cmd runs.");
    Out("  ClaudeWeekUsageTray.exe --self-test        Run the built-in tests.");
    Out("  ClaudeWeekUsageTray.exe --version");
    Out("");
    Out("  ClaudeWeekUsageTray.exe --statusline       Status-line mode. Claude Code runs this");
    Out("                                             for you; there is no reason to type it.");
    Out("");
    Out("The tray only ever receives four numbers: the used percentage and reset time for");
    Out("the 5-hour and 7-day windows. It does not read credentials and does not call any");
    Out("network service.");
}

int dispatch(HINSTANCE instance) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // Status-line mode short-circuits everything: Claude Code is waiting on
    // stdout, so nothing else may print or pop up.
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--statusline") {
            LocalFree(argv);
            return RunStatusLine();
        }
    }

    bool wantSetup = false;
    bool wantRemove = false;
    bool wantCleanup = false;
    bool wantUninstall = false;
    bool wantSelfTest = false;
    bool wantHelp = false;
    bool wantVersion = false;
    SetupOptions setupOptions;
    CleanupOptions cleanupOptions;
    bool unknown = false;
    std::wstring unknownArg;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--setup") {
            wantSetup = true;
        } else if (arg == L"--wrap-existing") {
            setupOptions.wrapExisting = true;
        } else if (arg == L"--remove-statusline") {
            wantRemove = true;
        } else if (arg == L"--cleanup-tray-icons") {
            wantCleanup = true;
        } else if (arg == L"--uninstall") {
            wantUninstall = true;
        } else if (arg == L"--apply") {
            cleanupOptions.apply = true;
        } else if (arg == L"--self-test") {
            wantSelfTest = true;
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            wantHelp = true;
        } else if (arg == L"--version") {
            wantVersion = true;
        } else {
            unknown = true;
            unknownArg = arg;
        }
    }
    if (argv != nullptr) LocalFree(argv);

    int result = 0;
    if (unknown) {
        Out("Unknown option: " + ToUtf8(unknownArg));
        printUsage();
        result = 2;
    } else if (wantHelp) {
        printUsage();
    } else if (wantVersion) {
        Out(ToUtf8(std::wstring(kVersion)));
    } else if (wantSelfTest) {
        result = RunSelfTest();
    } else if (wantSetup) {
        result = RunSetup(setupOptions);
    } else if (wantRemove) {
        result = RunRemoveStatusLine();
    } else if (wantCleanup) {
        result = RunCleanupTrayIcons(cleanupOptions);
    } else if (wantUninstall) {
        result = RunUninstall();
    } else {
        TrayApp app;
        return app.run(instance);
    }
    FlushOut();
    return result;
}

}  // namespace
}  // namespace cwut

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    cwut::enableDpiAwareness();
    return cwut::dispatch(instance);
}

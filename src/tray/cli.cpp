#include "cli.h"

#include <windows.h>

#include <string>
#include <vector>

#include "../common/json.h"
#include "../common/winutil.h"
#include "menu.h"

namespace cwut {
namespace {

HANDLE g_console = INVALID_HANDLE_VALUE;
bool g_consoleTried = false;
bool g_redirected = false;
std::wstring g_buffered;

const wchar_t* kNotifyIconSettings = L"Control Panel\\NotifyIconSettings";
const wchar_t* kTrayExeName = L"claudeweekusagetray.exe";
const wchar_t* kStatusLineFlag = L"--statusline";

std::wstring toLower(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& c : out) c = static_cast<wchar_t>(towlower(c));
    return out;
}

std::wstring baseName(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

std::wstring timestampSuffix() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t buf[32];
    swprintf_s(buf, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond);
    return buf;
}

std::wstring wrappedConfigPath() {
    std::wstring dir = GetAppDataDir();
    if (dir.empty()) return std::wstring();
    return dir + L"\\wrapped-statusline.json";
}

// True only when the command runs this program in status-line mode. Both
// halves must match so a user command that merely mentions one of them is not
// mistaken for ours.
bool commandIsOurs(const std::wstring& command) {
    const std::wstring lower = toLower(command);
    return lower.find(kTrayExeName) != std::wstring::npos &&
           lower.find(kStatusLineFlag) != std::wstring::npos;
}

}  // namespace

void Out(const std::string& line) {
    if (!g_consoleTried) {
        g_consoleTried = true;
        // Honour redirection first: a GUI-subsystem process still inherits a
        // piped stdout, and that is where `> out.txt` or a CI job expects text.
        HANDLE redirected = GetStdHandle(STD_OUTPUT_HANDLE);
        if (redirected != nullptr && redirected != INVALID_HANDLE_VALUE &&
            GetFileType(redirected) != FILE_TYPE_UNKNOWN) {
            g_console = redirected;
            g_redirected = true;
        } else {
            AttachConsole(ATTACH_PARENT_PROCESS);
            g_console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        }
    }
    if (g_console != INVALID_HANDLE_VALUE && g_redirected) {
        const std::string bytes = line + "\r\n";
        DWORD written = 0;
        WriteFile(g_console, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    } else if (g_console != INVALID_HANDLE_VALUE) {
        const std::wstring text = ToWide(line) + L"\r\n";
        DWORD written = 0;
        WriteConsoleW(g_console, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        g_buffered += ToWide(line) + L"\r\n";
    }
}

void FlushOut() {
    if (g_console == INVALID_HANDLE_VALUE && !g_buffered.empty()) {
        MessageBoxW(nullptr, g_buffered.c_str(), L"ClaudeWeekUsageTray", MB_OK | MB_ICONINFORMATION);
        g_buffered.clear();
    }
}

std::wstring StatusLineCommand() {
    const std::wstring exe = GetExecutablePath();
    if (exe.empty()) return std::wstring();
    // Quoted, because the path routinely contains spaces and Claude Code hands
    // the string to a shell.
    return L"\"" + exe + L"\" " + kStatusLineFlag;
}

// ---------------------------------------------------------------------------
// Status-line setup
// ---------------------------------------------------------------------------

namespace {

void printManualInstructions(const std::wstring& command) {
    Out("");
    Out("Nothing was changed. To connect the tray by hand, add this to the");
    Out("\"statusLine\" section of " + ToUtf8(GetClaudeSettingsPath()) + ":");
    Out("");
    Out("  \"statusLine\": {");
    Out("    \"type\": \"command\",");
    // Escaped so the line can be pasted into settings.json as it stands.
    Out("    \"command\": \"" + JsonEscape(ToUtf8(command)) + "\"");
    Out("  }");
    Out("");
    Out("If you already run your own status-line command, call it from a small");
    Out("script of your own that also pipes the same stdin into the command");
    Out("above. In status-line mode this program reads stdin, forwards two");
    Out("percentages and two reset times to the tray, and prints nothing else.");
}

bool backupSettings(const std::wstring& path, const std::string& contents, std::wstring& backupOut) {
    backupOut = path + L".cwut-backup-" + timestampSuffix();
    return WriteAllBytesAtomic(backupOut, contents, /*userOnly=*/false, nullptr);
}

}  // namespace

int RunSetup(const SetupOptions& options) {
    const std::wstring helper = StatusLineCommand();
    if (helper.empty()) {
        Out("Cannot determine this program's own path, so setup cannot continue.");
        return 2;
    }
    const std::wstring settingsPath = GetClaudeSettingsPath();
    if (settingsPath.empty()) {
        Out("Cannot locate your user profile, so Claude Code settings cannot be found.");
        return 2;
    }

    JsonValue root = JsonValue::makeObject();
    std::string existingText;
    const bool settingsExist = ReadAllBytes(settingsPath, existingText, kMaxJsonBytes);
    if (settingsExist && !existingText.empty()) {
        std::string error;
        if (!JsonParse(existingText, root, &error) || !root.isObject()) {
            Out("Your Claude Code settings file could not be read as JSON (" + error + ").");
            Out("Setup will not rewrite a file it cannot parse.");
            printManualInstructions(helper);
            return 3;
        }
    }

    const JsonValue* statusLine = root.find("statusLine");
    std::wstring previousCommand;
    bool repointOnly = false;
    if (statusLine != nullptr && !statusLine->isNull()) {
        if (!statusLine->isObject()) {
            Out("Your settings already contain a \"statusLine\" entry in a shape this setup does");
            Out("not understand, so it was left untouched.");
            printManualInstructions(helper);
            return 3;
        }
        const JsonValue* type = statusLine->find("type");
        const JsonValue* command = statusLine->find("command");
        if (type == nullptr || !type->isString() || type->str != "command" || command == nullptr ||
            !command->isString()) {
            Out("Your existing \"statusLine\" entry is not a simple command, so it was left");
            Out("untouched.");
            printManualInstructions(helper);
            return 3;
        }
        previousCommand = ToWide(command->str);
        if (commandIsOurs(previousCommand)) {
            if (toLower(previousCommand) == toLower(helper)) {
                Out("Claude Code is already configured to use this copy of ClaudeWeekUsageTray.");
                Out("Nothing to do.");
                return 0;
            }
            // Same program at a different path, so this is a re-point, not a
            // wrap. Any command already recorded stays recorded.
            repointOnly = true;
        }
        if (!repointOnly && !options.wrapExisting) {
            Out("Claude Code already runs a status-line command:");
            Out("  " + ToUtf8(previousCommand));
            Out("");
            Out("Setup will not replace it without your say-so. Re-run with --wrap-existing to");
            Out("keep that command running: the helper will pass the same input to it and print");
            Out("its output unchanged, while also forwarding the four usage numbers to the tray.");
            printManualInstructions(helper);
            return 3;
        }
    }

    // Record the command being wrapped before touching settings.json.
    const std::wstring wrappedPath = wrappedConfigPath();
    if (repointOnly) {
        // Leave the record alone: it still describes the user's own command,
        // not the copy of this program being replaced.
    } else if (!previousCommand.empty()) {
        JsonValue wrapped = JsonValue::makeObject();
        wrapped.set("version", JsonValue::makeNumber(1));
        wrapped.set("wrapped_command", JsonValue::makeString(ToUtf8(previousCommand)));
        std::string error;
        if (wrappedPath.empty() ||
            !WriteAllBytesAtomic(wrappedPath, JsonSerialize(wrapped), /*userOnly=*/true, &error)) {
            Out("Could not save your existing status-line command (" + error + ").");
            Out("Settings were left unchanged.");
            return 2;
        }
    } else if (!wrappedPath.empty()) {
        DeleteFileIfPresent(wrappedPath);
    }

    if (settingsExist) {
        std::wstring backup;
        if (!backupSettings(settingsPath, existingText, backup)) {
            Out("Could not write a backup of your settings file. Nothing was changed.");
            return 2;
        }
        Out("Backup written: " + ToUtf8(backup));
    }

    JsonValue entry = JsonValue::makeObject();
    entry.set("type", JsonValue::makeString("command"));
    entry.set("command", JsonValue::makeString(ToUtf8(helper)));
    if (statusLine != nullptr && statusLine->isObject()) {
        // Keep any extra keys the user had, such as "padding".
        for (const auto& kv : statusLine->object) {
            if (kv.first != "type" && kv.first != "command") entry.set(kv.first, kv.second);
        }
    }
    root.set("statusLine", entry);

    std::string error;
    if (!WriteAllBytesAtomic(settingsPath, JsonSerialize(root), /*userOnly=*/false, &error)) {
        Out("Could not write settings.json (" + error + "). Nothing was changed.");
        return 2;
    }

    Out("Status line configured in " + ToUtf8(settingsPath) + ".");
    if (repointOnly) {
        Out("Claude Code now runs this copy instead of:");
        Out("  " + ToUtf8(previousCommand));
    } else if (!previousCommand.empty()) {
        Out("Your previous command is still used and its output is unchanged:");
        Out("  " + ToUtf8(previousCommand));
    }
    Out("");
    Out("Start ClaudeWeekUsageTray.exe, then start Claude Code. The tray number appears");
    Out("as soon as Claude Code sends its first status-line payload.");
    return 0;
}

int RunRemoveStatusLine() {
    const std::wstring settingsPath = GetClaudeSettingsPath();
    std::string text;
    if (settingsPath.empty() || !ReadAllBytes(settingsPath, text, kMaxJsonBytes)) {
        Out("No Claude Code settings file was found. Nothing to undo.");
        return 0;
    }
    JsonValue root;
    std::string error;
    if (!JsonParse(text, root, &error) || !root.isObject()) {
        Out("Your settings file could not be read as JSON (" + error + "). Nothing was changed.");
        return 3;
    }
    JsonValue* statusLine = root.find("statusLine");
    if (statusLine == nullptr || !statusLine->isObject()) {
        Out("No status-line entry to remove. Nothing was changed.");
        return 0;
    }
    const JsonValue* command = statusLine->find("command");
    if (command == nullptr || !command->isString() || !commandIsOurs(ToWide(command->str))) {
        Out("The current status-line command is not the ClaudeWeekUsageTray helper, so it was");
        Out("left untouched.");
        return 3;
    }

    std::wstring backup;
    if (!backupSettings(settingsPath, text, backup)) {
        Out("Could not write a backup of your settings file. Nothing was changed.");
        return 2;
    }
    Out("Backup written: " + ToUtf8(backup));

    // Put back whatever we wrapped, if anything.
    std::string wrappedText;
    std::string restored;
    const std::wstring wrappedPath = wrappedConfigPath();
    if (!wrappedPath.empty() && ReadAllBytes(wrappedPath, wrappedText, 65536)) {
        JsonValue wrapped;
        if (JsonParse(wrappedText, wrapped, nullptr)) {
            const JsonValue* original = wrapped.find("wrapped_command");
            if (original != nullptr && original->isString() && !original->str.empty()) {
                restored = original->str;
            }
        }
    }

    if (restored.empty()) {
        root.erase("statusLine");
    } else {
        statusLine->set("command", JsonValue::makeString(restored));
    }

    if (!WriteAllBytesAtomic(settingsPath, JsonSerialize(root), /*userOnly=*/false, &error)) {
        Out("Could not write settings.json (" + error + "). Nothing was changed.");
        return 2;
    }
    if (!wrappedPath.empty()) DeleteFileIfPresent(wrappedPath);

    if (restored.empty()) {
        Out("Status-line entry removed.");
    } else {
        Out("Status line restored to your previous command:");
        Out("  " + restored);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// First-run check
// ---------------------------------------------------------------------------

StatusLineState ClassifyStatusLine(bool entryPresent, bool entryUnderstood,
                                   const std::wstring& command,
                                   const std::wstring& ourExecutablePath) {
    if (!entryPresent) return StatusLineState::Missing;
    if (!entryUnderstood) return StatusLineState::Unreadable;
    if (!commandIsOurs(command)) return StatusLineState::Foreign;
    if (ourExecutablePath.empty()) return StatusLineState::ConnectedElsewhere;
    return toLower(command).find(toLower(ourExecutablePath)) != std::wstring::npos
               ? StatusLineState::Connected
               : StatusLineState::ConnectedElsewhere;
}

StatusLineState InspectStatusLine(std::wstring& command) {
    command.clear();
    const std::wstring settingsPath = GetClaudeSettingsPath();
    std::string text;
    if (settingsPath.empty() || !ReadAllBytes(settingsPath, text, kMaxJsonBytes)) {
        return StatusLineState::Missing;
    }
    JsonValue root;
    if (!JsonParse(text, root, nullptr) || !root.isObject()) {
        return ClassifyStatusLine(true, false, L"", GetExecutablePath());
    }
    const JsonValue* entry = root.find("statusLine");
    if (entry == nullptr || entry->isNull()) {
        return StatusLineState::Missing;
    }
    if (!entry->isObject()) {
        return ClassifyStatusLine(true, false, L"", GetExecutablePath());
    }
    const JsonValue* type = entry->find("type");
    const JsonValue* value = entry->find("command");
    const bool understood = type != nullptr && type->isString() && type->str == "command" &&
                            value != nullptr && value->isString();
    if (understood) command = ToWide(value->str);
    return ClassifyStatusLine(true, understood, command, GetExecutablePath());
}

void OfferStatusLineSetup() {
    std::wstring command;
    const StatusLineState state = InspectStatusLine(command);
    if (state == StatusLineState::Connected) return;

    std::wstring message;
    switch (state) {
        case StatusLineState::Missing:
            message =
                L"Claude Code is not sending usage to this program yet, so the tray will "
                L"keep showing ––.\n\nConnect it now?\n\nThis adds a status-line "
                L"command to your Claude Code settings. The file is backed up first, and "
                L"Exit does not undo it — use uninstall.cmd for that.";
            break;
        case StatusLineState::Foreign:
            message =
                L"Claude Code is not sending usage to this program yet, so the tray will "
                L"keep showing ––.\n\nIt already runs a status-line command of its "
                L"own:\n\n    " +
                command +
                L"\n\nConnect this program as well?\n\nYour command keeps running and its "
                L"output does not change. Your settings file is backed up first.";
            break;
        case StatusLineState::ConnectedElsewhere:
            message =
                L"Claude Code is sending usage to a different copy of this program:\n\n    " +
                command +
                L"\n\nPoint it at the copy you just started instead?\n\nYour settings file is "
                L"backed up first.";
            break;
        case StatusLineState::Unreadable:
            MessageBoxW(nullptr,
                        L"Claude Code's settings file could not be read as JSON, so this "
                        L"program will not change it.\n\nRun ClaudeWeekUsageTray.exe --setup "
                        L"from a command prompt to see the lines to add by hand.",
                        L"ClaudeWeekUsageTray", MB_OK | MB_ICONWARNING);
            return;
        case StatusLineState::Connected: return;
    }

    if (MessageBoxW(nullptr, message.c_str(), L"ClaudeWeekUsageTray",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    SetupOptions options;
    options.wrapExisting = true;  // The prompt said the existing command is kept.
    const int result = RunSetup(options);
    FlushOut();
    if (result == 0) {
        MessageBoxW(nullptr,
                    L"Connected. The number appears the next time Claude Code draws its "
                    L"status line, which happens as soon as you use it.",
                    L"ClaudeWeekUsageTray", MB_OK | MB_ICONINFORMATION);
    }
}

// ---------------------------------------------------------------------------
// Notification-area setting cleanup
// ---------------------------------------------------------------------------

namespace {

struct NotifyEntry {
    std::wstring subKey;
    std::wstring executablePath;
};

std::wstring escapeRegString(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : text) {
        if (c == L'\\' || c == L'"') out.push_back(L'\\');
        out.push_back(c);
    }
    return out;
}

// Writes a .reg file that recreates the given subkeys exactly.
bool exportEntries(const std::vector<NotifyEntry>& entries, const std::wstring& path,
                   std::wstring& error) {
    std::wstring text = L"Windows Registry Editor Version 5.00\r\n";
    for (const NotifyEntry& entry : entries) {
        const std::wstring full = std::wstring(kNotifyIconSettings) + L"\\" + entry.subKey;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, full.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
            error = L"cannot read " + full;
            return false;
        }
        text += L"\r\n[HKEY_CURRENT_USER\\" + full + L"]\r\n";
        for (DWORD index = 0;; ++index) {
            wchar_t name[512];
            DWORD nameLength = 512;
            DWORD type = 0;
            DWORD dataLength = 0;
            LONG rc = RegEnumValueW(key, index, name, &nameLength, nullptr, &type, nullptr,
                                    &dataLength);
            if (rc == ERROR_NO_MORE_ITEMS) break;
            if (rc != ERROR_SUCCESS) break;
            std::vector<unsigned char> data(dataLength ? dataLength : 1);
            nameLength = 512;
            rc = RegEnumValueW(key, index, name, &nameLength, nullptr, &type, data.data(),
                               &dataLength);
            if (rc != ERROR_SUCCESS) continue;

            const std::wstring valueName = (nameLength == 0) ? L"@" : (L"\"" + std::wstring(name) + L"\"");
            if (type == REG_SZ || type == REG_EXPAND_SZ) {
                std::wstring value(reinterpret_cast<wchar_t*>(data.data()),
                                   dataLength / sizeof(wchar_t));
                while (!value.empty() && value.back() == L'\0') value.pop_back();
                if (type == REG_SZ) {
                    text += valueName + L"=\"" + escapeRegString(value) + L"\"\r\n";
                } else {
                    text += valueName + L"=hex(2):";
                    for (DWORD i = 0; i < dataLength; ++i) {
                        wchar_t byteText[8];
                        swprintf_s(byteText, L"%02x%ls", data[i], (i + 1 < dataLength) ? L"," : L"");
                        text += byteText;
                    }
                    text += L"\r\n";
                }
            } else if (type == REG_DWORD && dataLength == 4) {
                DWORD value = 0;
                memcpy(&value, data.data(), 4);
                wchar_t buf[32];
                swprintf_s(buf, L"dword:%08x", value);
                text += valueName + L"=" + buf + L"\r\n";
            } else {
                text += valueName + L"=hex(" ;
                wchar_t typeText[16];
                swprintf_s(typeText, L"%x", type);
                text += typeText;
                text += L"):";
                for (DWORD i = 0; i < dataLength; ++i) {
                    wchar_t byteText[8];
                    swprintf_s(byteText, L"%02x%ls", data[i], (i + 1 < dataLength) ? L"," : L"");
                    text += byteText;
                }
                text += L"\r\n";
            }
        }
        RegCloseKey(key);
    }

    // .reg files are read as UTF-16LE with a BOM.
    std::string bytes;
    bytes.push_back(static_cast<char>(0xFF));
    bytes.push_back(static_cast<char>(0xFE));
    bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    if (!WriteAllBytesAtomic(path, bytes, /*userOnly=*/false, nullptr)) {
        error = L"cannot write " + path;
        return false;
    }
    return true;
}

}  // namespace

int RunCleanupTrayIcons(const CleanupOptions& options) {
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kNotifyIconSettings, 0, KEY_READ, &root) !=
        ERROR_SUCCESS) {
        Out("No notification-area settings were found for your account. Nothing to clean up.");
        return 0;
    }

    const std::wstring currentExe = toLower(GetExecutablePath());
    std::vector<NotifyEntry> ours;
    for (DWORD index = 0;; ++index) {
        wchar_t name[512];
        DWORD nameLength = 512;
        LONG rc = RegEnumKeyExW(root, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr);
        if (rc != ERROR_SUCCESS) break;

        HKEY child = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &child) != ERROR_SUCCESS) continue;
        wchar_t pathBuffer[1024];
        DWORD pathBytes = sizeof(pathBuffer);
        DWORD type = 0;
        LONG got = RegQueryValueExW(child, L"ExecutablePath", nullptr, &type,
                                    reinterpret_cast<LPBYTE>(pathBuffer), &pathBytes);
        RegCloseKey(child);
        if (got != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;

        std::wstring executable(pathBuffer, pathBytes / sizeof(wchar_t));
        while (!executable.empty() && executable.back() == L'\0') executable.pop_back();
        // Only ever consider entries belonging to this program.
        if (toLower(baseName(executable)) != kTrayExeName) continue;
        ours.push_back({name, executable});
    }
    RegCloseKey(root);

    if (ours.empty()) {
        Out("No ClaudeWeekUsageTray entries were found in your notification-area settings.");
        return 0;
    }

    std::vector<NotifyEntry> stale;
    for (const NotifyEntry& entry : ours) {
        if (options.includeCurrent || toLower(entry.executablePath) != currentExe) {
            stale.push_back(entry);
        }
    }

    Out("ClaudeWeekUsageTray entries under HKEY_CURRENT_USER\\" + ToUtf8(std::wstring(kNotifyIconSettings)) + ":");
    for (const NotifyEntry& entry : ours) {
        const bool keep = !options.includeCurrent && toLower(entry.executablePath) == currentExe;
        Out(std::string(keep ? "  keep    " : "  remove  ") + ToUtf8(entry.executablePath));
    }
    if (stale.empty()) {
        Out("");
        Out("Nothing to remove: every entry points at the copy you are running now.");
        return 0;
    }
    if (!options.apply) {
        Out("");
        Out("This was a report only. Re-run with --apply to remove the entries marked stale.");
        Out("A .reg backup is written first so the removal can be undone.");
        return 0;
    }

    std::wstring backupPath = GetAppDataDir();
    if (backupPath.empty()) {
        Out("Cannot locate the application data folder, so no backup could be written.");
        return 2;
    }
    backupPath += L"\\notifyicon-backup-" + timestampSuffix() + L".reg";
    std::wstring error;
    if (!exportEntries(stale, backupPath, error)) {
        Out("Backup failed (" + ToUtf8(error) + "). Nothing was removed.");
        return 2;
    }
    Out("Backup written: " + ToUtf8(backupPath));

    int removed = 0;
    for (const NotifyEntry& entry : stale) {
        const std::wstring full = std::wstring(kNotifyIconSettings) + L"\\" + entry.subKey;
        if (RegDeleteKeyExW(HKEY_CURRENT_USER, full.c_str(), 0, 0) == ERROR_SUCCESS) {
            ++removed;
        } else {
            Out("Could not remove " + ToUtf8(entry.subKey) + ".");
        }
    }
    Out("Removed " + std::to_string(removed) + (removed == 1 ? " entry." : " entries.") +
        " No files were deleted.");
    Out("To undo, double-click the .reg backup above and sign out and back in.");
    return 0;
}

// ---------------------------------------------------------------------------
// Uninstall
// ---------------------------------------------------------------------------

int RunUninstall() {
    Out("Removing ClaudeWeekUsageTray from your account.");
    Out("");

    Out("1. Stopping the tray icon");
    HWND running = FindWindowW(kTrayWindowClass, nullptr);
    if (running != nullptr) {
        PostMessageW(running, WM_COMMAND, kCommandExit, 0);
        // Give the icon a moment to leave the notification area, so the
        // registry cleanup below sees the finished state.
        Sleep(700);
        Out("   Stopped.");
    } else {
        Out("   It was not running.");
    }
    Out("");

    Out("2. Claude Code status line");
    const int statusLineResult = RunRemoveStatusLine();
    Out("");

    Out("3. Notification-area entries");
    CleanupOptions cleanup;
    cleanup.apply = true;
    cleanup.includeCurrent = true;  // Uninstalling, so this copy goes too.
    const int cleanupResult = RunCleanupTrayIcons(cleanup);
    Out("");

    Out("4. What is left for you");
    Out("   Delete the folder this program is in.");
    Out("   Backups are kept in %LOCALAPPDATA%\\ClaudeWeekUsageTray; delete that");
    Out("   folder too if you do not want to keep them.");
    Out("");
    Out("This command deleted no files.");

    // A status line that was never ours, or an already clean registry, is not
    // a failure. Only a hard error is.
    return (statusLineResult == 2 || cleanupResult == 2) ? 2 : 0;
}

}  // namespace cwut

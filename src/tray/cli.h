// Console-mode commands offered by the tray executable.
#pragma once

#include <string>

namespace cwut {

// Window class of the resident tray process. A second launch and the uninstall
// command use it to find a copy that is already running.
inline constexpr const wchar_t* kTrayWindowClass = L"ClaudeWeekUsageTray.Message";

// Writes a line to the parent console when there is one, and otherwise
// collects the text for a single message box at exit.
void Out(const std::string& line);
void FlushOut();

struct SetupOptions {
    // Required before an existing user statusLine may be wrapped.
    bool wrapExisting = false;
};

// How Claude Code's status-line setting relates to this executable.
enum class StatusLineState {
    Connected,           // runs this exact copy; usage will arrive
    ConnectedElsewhere,  // runs this program, but a copy at another path
    Missing,             // no statusLine entry at all
    Foreign,             // runs some other command
    Unreadable,          // a settings file this program will not rewrite
};

// Pure classification, kept separate from any file so it can be tested.
StatusLineState ClassifyStatusLine(bool entryPresent, bool entryUnderstood,
                                   const std::wstring& command,
                                   const std::wstring& ourExecutablePath);

// Reads the settings file and classifies it. `command` receives whatever
// command was found, empty when there is none.
StatusLineState InspectStatusLine(std::wstring& command);

// Called once when the tray starts. If usage cannot reach it, this says so and
// offers to fix it. Nothing is written unless the user agrees.
void OfferStatusLineSetup();

int RunSetup(const SetupOptions& options);
int RunRemoveStatusLine();

struct CleanupOptions {
    bool apply = false;  // Without this the command only reports.
    // De-duplication keeps the entry belonging to the copy being run. Uninstall
    // wants that one gone as well.
    bool includeCurrent = false;
};

int RunCleanupTrayIcons(const CleanupOptions& options);

// Undoes the status-line setup and removes leftover notification-area entries
// in one step, then explains what is left to delete by hand.
int RunUninstall();

int RunSelfTest();

// The exact command Claude Code is told to run: this executable's full path,
// quoted, followed by --statusline.
std::wstring StatusLineCommand();

}  // namespace cwut

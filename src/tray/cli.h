// Console-mode commands offered by the tray executable.
#pragma once

#include <string>

namespace cwut {

// Writes a line to the parent console when there is one, and otherwise
// collects the text for a single message box at exit.
void Out(const std::string& line);
void FlushOut();

struct SetupOptions {
    // Required before an existing user statusLine may be wrapped.
    bool wrapExisting = false;
};

int RunSetup(const SetupOptions& options);
int RunRemoveStatusLine();

struct CleanupOptions {
    bool apply = false;  // Without this the command only reports.
};

int RunCleanupTrayIcons(const CleanupOptions& options);

int RunSelfTest();

std::wstring StatusLineHelperPath();

}  // namespace cwut

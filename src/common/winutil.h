// Small Windows helpers shared by the tray and the status-line helper.
#pragma once

#include <string>

namespace cwut {

std::string ToUtf8(const std::wstring& text);
std::wstring ToWide(const std::string& text);

// Reads a whole file as bytes. Returns false if it is missing, unreadable, or
// larger than maxBytes.
bool ReadAllBytes(const std::wstring& path, std::string& out, size_t maxBytes);

// Writes via a temporary file in the same directory, then replaces the target.
// When userOnly is true the file is created with a DACL that grants access to
// the current user account only.
bool WriteAllBytesAtomic(const std::wstring& path, const std::string& data, bool userOnly,
                         std::string* error);

bool FileExists(const std::wstring& path);
bool DeleteFileIfPresent(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);

std::wstring GetExecutablePath();
std::wstring GetExecutableDir();

// %LOCALAPPDATA%\ClaudeWeekUsageTray, created on demand.
std::wstring GetAppDataDir();

// %USERPROFILE%\.claude and its settings.json.
std::wstring GetClaudeDir();
std::wstring GetClaudeSettingsPath();

std::wstring GetCurrentUserSidString();

// Compares two strings without an early exit, for token checks.
bool ConstantTimeEquals(const std::string& a, const std::string& b);

// Hex-encoded cryptographically random bytes.
std::string RandomHexToken(size_t byteCount);

// True when Windows is using the light system theme for the notification area.
bool SystemUsesLightTheme();

}  // namespace cwut

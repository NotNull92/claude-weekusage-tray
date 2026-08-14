#include "winutil.h"

#include <windows.h>

#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstdio>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace cwut {
namespace {

std::wstring joinPath(const std::wstring& dir, const std::wstring& leaf) {
    if (dir.empty()) return leaf;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + leaf;
    return dir + L"\\" + leaf;
}

std::wstring environmentValue(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return std::wstring();
    std::vector<wchar_t> buffer(needed);
    DWORD written = GetEnvironmentVariableW(name, buffer.data(), needed);
    if (written == 0 || written >= needed) return std::wstring();
    return std::wstring(buffer.data(), written);
}

std::wstring parentDirectory(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return std::wstring();
    return path.substr(0, cut);
}

}  // namespace

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], needed,
                        nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                     nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], needed);
    return out;
}

bool ReadAllBytes(const std::wstring& path, std::string& out, size_t maxBytes) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > maxBytes) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD chunk = static_cast<DWORD>(
            (out.size() - offset) > 0x10000 ? 0x10000 : (out.size() - offset));
        DWORD read = 0;
        if (!ReadFile(file, &out[offset], chunk, &read, nullptr) || read == 0) {
            CloseHandle(file);
            out.clear();
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    return true;
}

bool WriteAllBytesAtomic(const std::wstring& path, const std::string& data, bool userOnly,
                         std::string* error) {
    const std::wstring dir = parentDirectory(path);
    if (!dir.empty() && !EnsureDirectory(dir)) {
        if (error) *error = "cannot create directory";
        return false;
    }
    const std::wstring temp = path + L".tmp";

    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES* saPtr = nullptr;
    if (userOnly) {
        const std::wstring sid = GetCurrentUserSidString();
        if (!sid.empty()) {
            // Protected DACL: inheritance blocked, current user only.
            const std::wstring sddl = L"D:P(A;;FA;;;" + sid + L")";
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
                                                                     SDDL_REVISION_1,
                                                                     &descriptor, nullptr)) {
                sa.nLength = sizeof(sa);
                sa.lpSecurityDescriptor = descriptor;
                sa.bInheritHandle = FALSE;
                saPtr = &sa;
            }
        }
        if (saPtr == nullptr) {
            if (error) *error = "cannot build user-only security descriptor";
            return false;
        }
    }

    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, saPtr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (descriptor != nullptr) LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot create temporary file";
        return false;
    }
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD chunk = static_cast<DWORD>(
            (data.size() - offset) > 0x10000 ? 0x10000 : (data.size() - offset));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) || written == 0) {
            CloseHandle(file);
            DeleteFileW(temp.c_str());
            if (error) *error = "write failed";
            return false;
        }
        offset += written;
    }
    FlushFileBuffers(file);
    CloseHandle(file);

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        if (error) *error = "replace failed";
        return false;
    }
    return true;
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DeleteFileIfPresent(const std::wstring& path) {
    if (!FileExists(path)) return true;
    return DeleteFileW(path.c_str()) != 0;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const std::wstring parent = parentDirectory(path);
    if (!parent.empty() && parent != path && parent.find(L'\\') != std::wstring::npos) {
        EnsureDirectory(parent);
    }
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring GetExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return std::wstring();
        if (written < buffer.size() - 1) return std::wstring(buffer.data(), written);
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 65536) return std::wstring();
    }
}

std::wstring GetExecutableDir() { return parentDirectory(GetExecutablePath()); }

std::wstring GetAppDataDir() {
    std::wstring base = environmentValue(L"LOCALAPPDATA");
    if (base.empty()) {
        PWSTR folder = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
            base = folder;
            CoTaskMemFree(folder);
        }
    }
    if (base.empty()) return std::wstring();
    std::wstring dir = joinPath(base, L"ClaudeWeekUsageTray");
    EnsureDirectory(dir);
    return dir;
}

std::wstring GetClaudeDir() {
    std::wstring home = environmentValue(L"USERPROFILE");
    if (home.empty()) {
        PWSTR folder = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &folder))) {
            home = folder;
            CoTaskMemFree(folder);
        }
    }
    if (home.empty()) return std::wstring();
    return joinPath(home, L".claude");
}

std::wstring GetClaudeSettingsPath() {
    std::wstring dir = GetClaudeDir();
    if (dir.empty()) return std::wstring();
    return joinPath(dir, L"settings.json");
}

std::wstring GetCurrentUserSidString() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::wstring();
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    if (needed == 0) {
        CloseHandle(token);
        return std::wstring();
    }
    std::vector<unsigned char> buffer(needed);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
        CloseHandle(token);
        return std::wstring();
    }
    CloseHandle(token);
    TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &text)) return std::wstring();
    std::wstring result = text;
    LocalFree(text);
    return result;
}

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<unsigned char>(diff | (static_cast<unsigned char>(a[i]) ^
                                                  static_cast<unsigned char>(b[i])));
    }
    return diff == 0;
}

std::string RandomHexToken(size_t byteCount) {
    std::vector<unsigned char> bytes(byteCount);
    NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) return std::string();
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(byteCount * 2);
    for (unsigned char b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

bool SystemUsesLightTheme() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, L"SystemUsesLightTheme", nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_DWORD) return false;
    return value != 0;
}

}  // namespace cwut

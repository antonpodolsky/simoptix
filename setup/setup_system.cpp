// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_system.h"

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <filesystem>
#include <system_error>
#include <vector>

#include "shared/unique_handle.h"

namespace {

    std::wstring KnownFolder(REFKNOWNFOLDERID id) {
        wchar_t* path = nullptr;
        std::wstring result;

        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) {
            result = path;
        }

        CoTaskMemFree(path);  // also when the call failed, as its documentation asks

        return result;
    }

    // The default value of HKEY_LOCAL_MACHINE\<key>, a string, into `data`, or only its size when `data`
    // is nullptr.
    LSTATUS GetDefaultString(const wchar_t* key, wchar_t* data, DWORD* bytes) {
        return RegGetValueW(HKEY_LOCAL_MACHINE, key, nullptr, RRF_RT_REG_SZ, nullptr, data, bytes);
    }

    // The default value of HKEY_LOCAL_MACHINE\<key>, a string. Empty when there is none.
    std::wstring ReadDefaultString(const wchar_t* key) {
        DWORD bytes = 0;

        if (GetDefaultString(key, nullptr, &bytes) != ERROR_SUCCESS) {
            return L"";
        }

        std::wstring value(bytes / sizeof(wchar_t), L'\0');

        if (GetDefaultString(key, value.data(), &bytes) != ERROR_SUCCESS) {
            return L"";
        }

        value.resize(wcsnlen(value.c_str(), value.size()));

        return value;
    }

    // The file's version resource, empty when it has none.
    std::vector<char> ReadVersionBlock(const std::wstring& path) {
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), nullptr);

        if (size == 0) {
            return {};
        }

        std::vector<char> block(size);

        if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) {
            return {};
        }

        return block;
    }

    bool ReadFileVersion(const std::wstring& path, VS_FIXEDFILEINFO* version) {
        std::vector<char> block = ReadVersionBlock(path);

        if (block.empty()) {
            return false;
        }

        void* fixed = nullptr;
        UINT fixedSize = 0;
        const BOOL found = VerQueryValueW(block.data(), L"\\", &fixed, &fixedSize);

        if (!found || fixedSize < sizeof(VS_FIXEDFILEINFO)) {
            return false;
        }

        *version = *static_cast<const VS_FIXEDFILEINFO*>(fixed);

        return true;
    }

}

std::wstring DescribeLastError() {
    const DWORD code = GetLastError();
    wchar_t text[512] = {};

    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        text,
        static_cast<DWORD>(std::size(text)),
        nullptr
    );

    // Windows ends its text with a line break.
    std::wstring message(text, length);
    message.erase(message.find_last_not_of(L"\r\n ") + 1);

    return message + L" (" + std::to_wstring(code) + L")";
}

std::wstring ProgramFilesFolder() {
    return KnownFolder(FOLDERID_ProgramFiles);
}

std::wstring ProgramDataFolder() {
    return KnownFolder(FOLDERID_ProgramData);
}

std::wstring OwnExePath() {
    std::wstring path(MAX_PATH, L'\0');

    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

        if (length < path.size()) {
            path.resize(length);
            return path;
        }

        path.resize(path.size() * 2);
    }
}

ObsInstall FindObs() {
    ObsInstall obs;
    std::wstring folder = ReadDefaultString(L"SOFTWARE\\OBS Studio");

    if (folder.empty()) {
        folder = ProgramFilesFolder() + L"\\obs-studio";
    }

    VS_FIXEDFILEINFO version = {};

    if (!ReadFileVersion(folder + L"\\bin\\64bit\\obs64.exe", &version)) {
        return obs;
    }

    // obs64.exe 32.2.2 carries its version only as the file version. Its product version is 0.0.0.
    obs.found = true;
    obs.major = HIWORD(version.dwFileVersionMS);

    return obs;
}

std::wstring ReadProductVersion(const std::wstring& path) {
    std::vector<char> block = ReadVersionBlock(path);

    if (block.empty()) {
        return L"";
    }

    const wchar_t* translations = L"\\VarFileInfo\\Translation";
    void* translation = nullptr;
    UINT translationSize = 0;
    const BOOL found = VerQueryValueW(block.data(), translations, &translation, &translationSize);

    if (!found || translationSize < 2 * sizeof(WORD)) {
        return L"";
    }

    // The first language and code page the file lists: 0x409 and 1200 in SimOptix's (version_info.rc).
    const WORD* language = static_cast<const WORD*>(translation);
    wchar_t query[64] = {};
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\ProductVersion", language[0], language[1]);

    void* text = nullptr;
    UINT length = 0;

    if (!VerQueryValueW(block.data(), query, &text, &length) || length == 0) {
        return L"";
    }

    return std::wstring(static_cast<const wchar_t*>(text));
}

bool IsProcessRunning(const wchar_t* exeName) {
    const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));

    if (!snapshot) {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    for (BOOL more = Process32FirstW(snapshot.Get(), &entry); more;
         more = Process32NextW(snapshot.Get(), &entry)) {
        if (EqualsNoCase(entry.szExeFile, exeName)) {
            return true;
        }
    }

    return false;
}

bool CreateFolders(const std::wstring& path, std::wstring* error) {
    std::error_code code;
    std::filesystem::create_directories(path, code);

    if (code) {
        *error = L"Can't make the folder " + path + L".";
        return false;
    }

    return true;
}

bool RemoveFolderTree(const std::wstring& path, std::wstring* error) {
    std::error_code code;
    std::filesystem::remove_all(path, code);

    if (code) {
        *error = L"Can't remove the folder " + path + L". A program may have a file in it open.";
        return false;
    }

    return true;
}

void RemoveFolderIfEmpty(const std::wstring& path) {
    (void)RemoveDirectoryW(path.c_str());  // fails, as wanted, while the folder holds anything
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool EqualsNoCase(const std::wstring& text, const std::wstring& other) {
    const int result = CompareStringOrdinal(
        text.c_str(),
        static_cast<int>(text.size()),
        other.c_str(),
        static_cast<int>(other.size()),
        TRUE
    );

    return result == CSTR_EQUAL;
}

bool EndsWithNoCase(const std::wstring& text, const std::wstring& ending) {
    return text.size() >= ending.size() && EqualsNoCase(text.substr(text.size() - ending.size()), ending);
}

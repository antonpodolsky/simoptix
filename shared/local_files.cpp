// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/local_files.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

    struct Kind {
        const wchar_t* folder;  // under %LOCALAPPDATA%\simoptix, or empty
        const wchar_t* name;
        const wchar_t* extension;
        int kept;
    };

    // In LocalFileKind's order. Each kind keeps 10, as OBS keeps 10 of its own logs. A trace is 100-300 MB
    // per hour, a dump a few MB, a layer log a few KB.
    constexpr Kind kKinds[] = {
        {L"traces", L"trace", L".csv", 10},
        {L"", L"obs", L".dmp", 10},
        {L"", L"openxr_frame_source", L".log", 10},
    };

    constexpr size_t kTimeChars = 15;  // yyyymmdd_hhmmss

    const Kind& KindOf(LocalFileKind kind) {
        return kKinds[static_cast<int>(kind)];
    }

    // "%LOCALAPPDATA%\simoptix[\<folder>]", made if missing. False if it doesn't fit in `size` characters.
    bool Folder(const Kind& kind, wchar_t* path, size_t size) {
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(size));

        if (n == 0 || n >= size) {
            return false;
        }

        if (wcscat_s(path, size, L"\\simoptix") != 0) {
            return false;
        }

        CreateDirectoryW(path, nullptr);  // fails harmlessly if it exists

        if (!kind.folder[0]) {
            return true;
        }

        if (wcscat_s(path, size, L"\\") != 0 || wcscat_s(path, size, kind.folder) != 0) {
            return false;
        }

        CreateDirectoryW(path, nullptr);
        return true;
    }

    // The length of the part of `file` that names its kind and second ("trace_20261109_133311"), or 0 if
    // the name is of another form: the kind's name, the time, then '_' or '.', ending in its extension.
    size_t StemLength(const Kind& kind, const wchar_t* file) {
        const size_t nameChars = wcslen(kind.name);
        const size_t stem = nameChars + 1 + kTimeChars;
        const size_t fileChars = wcslen(file);
        const size_t extensionChars = wcslen(kind.extension);

        if (fileChars < stem + extensionChars || wcsncmp(file, kind.name, nameChars) != 0
            || file[nameChars] != L'_') {
            return 0;
        }

        for (size_t i = 0; i < kTimeChars; i++) {
            const wchar_t c = file[nameChars + 1 + i];
            const bool ok = i == 8 ? c == L'_' : c >= L'0' && c <= L'9';

            if (!ok) {
                return 0;
            }
        }

        if (file[stem] != L'_' && file[stem] != L'.') {
            return 0;
        }

        if (_wcsicmp(file + fileChars - extensionChars, kind.extension) != 0) {
            return 0;
        }

        return stem;
    }

    // Every file of `kind` in `folder`, by name.
    std::vector<std::wstring> FilesOf(const Kind& kind, const std::wstring& folder) {
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW found;
        const HANDLE find = FindFirstFileW((folder + L"\\" + kind.name + L"_*").c_str(), &found);

        if (find == INVALID_HANDLE_VALUE) {
            return files;
        }

        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && StemLength(kind, found.cFileName)) {
                files.push_back(found.cFileName);
            }
        } while (FindNextFileW(find, &found));

        FindClose(find);
        std::sort(files.begin(), files.end());
        return files;
    }

    // The kind's seconds, oldest first: the files of one second share their stem.
    std::vector<std::wstring> StemsOf(const Kind& kind, const std::vector<std::wstring>& files) {
        std::vector<std::wstring> stems;

        for (const std::wstring& file : files) {
            std::wstring stem = file.substr(0, StemLength(kind, file.c_str()));

            if (stems.empty() || stems.back() != stem) {
                stems.push_back(std::move(stem));
            }
        }

        return stems;
    }

}

int LocalFilesKept(LocalFileKind kind) {
    return KindOf(kind).kept;
}

bool NewLocalFilePath(LocalFileKind kind, wchar_t* path, size_t size) {
    const Kind& k = KindOf(kind);

    if (!Folder(k, path, size)) {
        return false;
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    const size_t used = wcslen(path);
    const int n = swprintf(
        path + used,
        size - used,
        L"\\%s_%04d%02d%02d_%02d%02d%02d",
        k.name,
        t.wYear,
        t.wMonth,
        t.wDay,
        t.wHour,
        t.wMinute,
        t.wSecond
    );

    return n > 0;
}

int PruneLocalFiles(LocalFileKind kind, int room) {
    const Kind& k = KindOf(kind);
    wchar_t folder[MAX_PATH];

    if (!Folder(k, folder, MAX_PATH)) {
        return 0;
    }

    const std::vector<std::wstring> files = FilesOf(k, folder);
    const std::vector<std::wstring> stems = StemsOf(k, files);
    const int excess = static_cast<int>(stems.size()) + room - k.kept;

    if (excess <= 0) {
        return 0;
    }

    const std::vector<std::wstring> doomed(
        stems.begin(),
        stems.begin() + std::min<size_t>(excess, stems.size())
    );
    int deleted = 0;

    for (const std::wstring& stem : doomed) {
        bool all = true;

        for (const std::wstring& file : files) {
            if (file.compare(0, stem.size(), stem) == 0 && StemLength(k, file.c_str()) == stem.size()) {
                all = DeleteFileW((std::wstring(folder) + L"\\" + file).c_str()) && all;  // in use: stays
            }
        }

        deleted += all;
    }

    return deleted;
}

bool NewestLocalFile(LocalFileKind kind, wchar_t* path, size_t size) {
    const Kind& k = KindOf(kind);
    wchar_t folder[MAX_PATH];

    if (!Folder(k, folder, MAX_PATH)) {
        return false;
    }

    const std::vector<std::wstring> files = FilesOf(k, folder);

    if (files.empty()) {
        return false;
    }

    const std::wstring newest = std::wstring(folder) + L"\\" + files.back();

    if (newest.size() >= size) {
        return false;
    }

    wcscpy_s(path, size, newest.c_str());
    return true;
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_payload.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "setup_system.h"
#include "shared/unique_handle.h"

bool FindEmbeddedFile(const wchar_t* resource, EmbeddedFile* file, std::wstring* error) {
    // Resources of the exe itself need no freeing: they stay mapped until the process ends.
    const HRSRC info = FindResourceW(nullptr, resource, RT_RCDATA);

    if (!info) {
        *error = std::wstring(L"This setup is missing its file ") + resource + L".";
        return false;
    }

    const HGLOBAL loaded = LoadResource(nullptr, info);
    const void* data = loaded ? LockResource(loaded) : nullptr;

    if (!data) {
        *error = std::wstring(L"This setup can't read its file ") + resource + L".";
        return false;
    }

    file->data = data;
    file->size = SizeofResource(nullptr, info);

    return true;
}

bool HasSameBytes(const std::wstring& path, const EmbeddedFile& file) {
    const UniqueHandle handle(
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)
    );

    if (!handle) {
        return false;
    }

    LARGE_INTEGER size = {};

    if (!GetFileSizeEx(handle.Get(), &size) || static_cast<uint64_t>(size.QuadPart) != file.size) {
        return false;
    }

    std::vector<char> bytes(file.size);
    const DWORD wanted = static_cast<DWORD>(bytes.size());
    DWORD read = 0;
    const BOOL readAll = ReadFile(handle.Get(), bytes.data(), wanted, &read, nullptr);

    if (!readAll || read != bytes.size()) {
        return false;
    }

    return std::memcmp(bytes.data(), file.data, file.size) == 0;
}

namespace {

    // Writes the file at `path`, making it or emptying it first. The handle is closed on return.
    bool WriteWholeFile(const EmbeddedFile& file, const std::wstring& path, std::wstring* error) {
        const UniqueHandle handle(
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)
        );

        if (!handle) {
            *error = L"Can't write " + path + L": " + DescribeLastError();
            return false;
        }

        const DWORD size = static_cast<DWORD>(file.size);
        DWORD written = 0;
        const BOOL wroteAll = WriteFile(handle.Get(), file.data, size, &written, nullptr);

        if (!wroteAll || written != file.size) {
            *error = L"Can't write " + path + L": " + DescribeLastError();
            return false;
        }

        return true;
    }

}

bool WriteEmbeddedFile(const EmbeddedFile& file, const std::wstring& path, std::wstring* error) {
    const std::wstring temporary = path + L".new";

    // A leftover temporary file would only confuse, and the old file stays as it was.
    if (!WriteWholeFile(file, temporary, error)) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        *error = L"Can't replace " + path + L": " + DescribeLastError();
        (void)DeleteFileW(temporary.c_str());
        return false;
    }

    return true;
}

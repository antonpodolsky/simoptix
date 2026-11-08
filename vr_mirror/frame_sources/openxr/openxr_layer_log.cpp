// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_layer_log.h"

#include <windows.h>
#include <share.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "shared/log.h"
#include "shared/unique_handle.h"

namespace {

    constexpr int kMaxLines = 64;    // lines waiting for the writer; the layer logs a few per minute
    constexpr int kLineChars = 320;  // with the time of day in front

    struct Lines {
        int count = 0;
        char text[kMaxLines][kLineChars];
    };

    // Lines waiting to be written. The game's threads add with try_lock only; the writer takes them all
    // at once with a brief lock (a copy of at most 20 KB).
    std::mutex g_pendingMutex;
    Lines g_pending;
    std::atomic<uint32_t> g_skipped{0};  // lines dropped because the writer was busy or the memory full
    HANDLE g_wake = nullptr;             // set when a line is added; the writer sleeps on it

    // The writer thread's own.
    FILE* g_file = nullptr;
    Lines g_writing;

    void AddLine(const char* message) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::unique_lock<std::mutex> lock(g_pendingMutex, std::try_to_lock);
        if (!lock.owns_lock() || g_pending.count == kMaxLines) {
            g_skipped++;
            return;
        }

        snprintf(
            g_pending.text[g_pending.count++],
            kLineChars,
            "%02d:%02d:%02d.%03d  %s",
            t.wHour,
            t.wMinute,
            t.wSecond,
            t.wMilliseconds,
            message
        );

        lock.unlock();
        SetEvent(g_wake);
    }

    void Discard(const char*) {}

    DWORD WINAPI WriterThread(void*) {
        try {
            uint32_t skippedReported = 0;
            for (;;) {
                WaitForSingleObject(g_wake, INFINITE);
                {
                    std::lock_guard<std::mutex> lock(g_pendingMutex);
                    g_writing.count = g_pending.count;
                    memcpy(g_writing.text, g_pending.text, sizeof(g_writing.text[0]) * g_pending.count);
                    g_pending.count = 0;
                }

                for (int i = 0; i < g_writing.count; i++) {
                    fprintf(g_file, "%s\n", g_writing.text[i]);
                }
                const uint32_t skipped = g_skipped.load();
                if (skipped != skippedReported) {
                    fprintf(g_file, "(%u log line(s) skipped so far: the writer was busy)\n", skipped);
                    skippedReported = skipped;
                }

                fflush(g_file);  // the game may be closed or crash at any moment
            }
        } catch (...) {
            SetCoreLog(Discard);
        }

        return 0;
    }

}

void NoteDllLoaded() {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (n == 0 || n + 64 >= MAX_PATH) {
        return;
    }
    wcscat_s(path, L"\\simoptix");
    CreateDirectoryW(path, nullptr);  // fails harmlessly if it exists
    wcscat_s(path, L"\\openxr_frame_source_loads.log");
    UniqueHandle f(CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));

    if (!f) {
        return;
    }
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        exe[0] = L'\0';
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[MAX_PATH + 96];  // the program's path tells the game from the check program
    const int len = snprintf(
        line,
        sizeof(line),
        "%04d-%02d-%02d %02d:%02d:%02d.%03d  loaded into process %lu, %ls\r\n",
        t.wYear,
        t.wMonth,
        t.wDay,
        t.wHour,
        t.wMinute,
        t.wSecond,
        t.wMilliseconds,
        GetCurrentProcessId(),
        exe
    );

    DWORD written = 0;
    if (len > 0) {
        WriteFile(f.Get(), line, static_cast<DWORD>(len), &written, nullptr);
    }
}

bool StartLayerLog() {
    if (g_file) {
        return true;
    }
    SetCoreLog(Discard);  // never stdout: the game owns it
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    std::wstring dir = std::wstring(base) + L"\\simoptix";
    CreateDirectoryW(dir.c_str(), nullptr);  // fails harmlessly if it exists
    std::wstring path = dir + L"\\openxr_frame_source.log";
    MoveFileExW(path.c_str(), (dir + L"\\openxr_frame_source.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
    if (!g_file) {
        return false;
    }

    // The writer runs until the game exits, so the DLL must stay loaded that long, even if the OpenXR
    // loader lets go of it when the game destroys its instance.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&StartLayerLog),
            &self
        )) {
        return false;
    }

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
    if (!g_wake) {
        return false;
    }

    // runs on its own
    const UniqueHandle thread(CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr));
    if (!thread) {
        return false;
    }

    SetCoreLog(AddLine);
    return true;
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_layer_log.h"

#include <windows.h>
#include <share.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "shared/local_files.h"
#include "shared/log.h"
#include "shared/unique_handle.h"

namespace {

    constexpr int kMaxLines = 64;    // lines waiting; the layer logs a few per minute
    constexpr int kMaxHeld = 48;     // ...of them held before a reader says: the rest is room for failures
    constexpr int kLineChars = 320;  // with the time of day in front

    // What happens to a line that isn't a failure, set by what VR Mirror's reader says.
    enum Mode : int {
        kHold,   // no reader yet: kept in memory until one says
        kWrite,  // the last reader had Debug on
        kDrop,   // the last reader had Debug off
    };

    struct Lines {
        int count = 0;
        bool failure[kMaxLines] = {};  // written whatever the mode
        char text[kMaxLines][kLineChars];
    };

    std::atomic<int> g_mode{kHold};

    // Lines waiting to be written or held. The game's threads add with try_lock only; the writer takes
    // them with a brief lock (a copy of at most 20 KB).
    std::mutex g_pendingMutex;
    Lines g_pending;
    std::atomic<uint32_t> g_skipped{0};  // lines dropped because the writer was busy or the memory full
    HANDLE g_wake = nullptr;             // set when there is something to write; the writer sleeps on it
    bool g_started = false;              // StartLayerLog's work is done

    // This start's log, named once by NoteDllLoaded or the writer.
    wchar_t g_path[MAX_PATH] = {};

    // The writer thread's own.
    FILE* g_file = nullptr;
    Lines g_writing;

    // This start's log's path in g_path, made once.
    bool MakePath() {
        if (g_path[0]) {
            return true;
        }

        if (!NewLocalFilePath(LocalFileKind::kLayerLog, g_path, MAX_PATH)
            || wcscat_s(g_path, MAX_PATH, L".log") != 0) {
            g_path[0] = L'\0';
            return false;
        }

        return true;
    }

    // Under g_pendingMutex.
    void Push(bool failure, const char* text) {
        g_pending.failure[g_pending.count] = failure;
        strncpy_s(g_pending.text[g_pending.count], kLineChars, text, _TRUNCATE);
        g_pending.count++;
    }

    void Add(bool failure, const char* message) {
        const int mode = g_mode.load();
        if (!failure && mode == kDrop) {
            return;
        }
        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[kLineChars];
        snprintf(
            line,
            sizeof(line),
            "%02d:%02d:%02d.%03d  %s",
            t.wHour,
            t.wMinute,
            t.wSecond,
            t.wMilliseconds,
            message
        );

        const int room = failure || mode == kWrite ? kMaxLines : kMaxHeld;
        std::unique_lock<std::mutex> lock(g_pendingMutex, std::try_to_lock);
        if (!lock.owns_lock() || g_pending.count >= room) {
            g_skipped++;
            return;
        }
        Push(failure, line);
        lock.unlock();

        if (failure || mode == kWrite) {
            SetEvent(g_wake);
        }
    }

    void AddLine(const char* message) {
        Add(false, message);
    }

    void AddFailure(const char* message) {
        Add(true, message);
    }

    void Discard(const char*) {}

    // Under g_pendingMutex: moves the lines to write now into g_writing, keeps the held ones, and lets go
    // of the dropped ones.
    void TakeLines(int mode) {
        g_writing.count = 0;
        int kept = 0;

        for (int i = 0; i < g_pending.count; i++) {
            if (g_pending.failure[i] || mode == kWrite) {
                memcpy(g_writing.text[g_writing.count++], g_pending.text[i], kLineChars);
            } else if (mode == kHold) {
                if (kept != i) {
                    g_pending.failure[kept] = false;
                    memcpy(g_pending.text[kept], g_pending.text[i], kLineChars);
                }
                kept++;
            }
        }

        g_pending.count = kept;
    }

    // The file, made at the first line written: then the oldest logs go, and how many is noted.
    bool OpenLog(int* deleted) {
        if (g_file) {
            return true;
        }
        if (!MakePath()) {
            return false;
        }

        g_file = _wfsopen(g_path, L"a", _SH_DENYWR);
        if (!g_file) {
            return false;
        }

        *deleted = PruneLocalFiles(LocalFileKind::kLayerLog, 0);
        return true;
    }

    DWORD WINAPI WriterThread(void*) {
        try {
            uint32_t skippedReported = 0;

            for (;;) {
                WaitForSingleObject(g_wake, INFINITE);
                {
                    std::lock_guard<std::mutex> lock(g_pendingMutex);
                    TakeLines(g_mode.load());
                }

                int deleted = 0;
                if (g_writing.count == 0 || !OpenLog(&deleted)) {
                    continue;
                }

                for (int i = 0; i < g_writing.count; i++) {
                    fprintf(g_file, "%s\n", g_writing.text[i]);
                }
                if (deleted > 0) {
                    fprintf(g_file, "(%d older log(s) deleted)\n", deleted);
                }
                const uint32_t skipped = g_skipped.load();
                if (skipped != skippedReported) {
                    fprintf(
                        g_file,
                        "(%u log line(s) skipped so far: the writer was busy or the memory full)\n",
                        skipped
                    );
                    skippedReported = skipped;
                }

                fflush(g_file);  // the game may be closed or crash at any moment
            }
        } catch (...) {
            SetCoreLog(Discard);
            SetCoreLogError(Discard);
        }

        return 0;
    }

}

void NoteDllLoaded() {
    MakePath();  // named by the load's time

    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        exe[0] = L'\0';
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[kLineChars];  // the program's path tells the game from the check program
    snprintf(
        line,
        sizeof(line),
        "%04d-%02d-%02d %02d:%02d:%02d.%03d  loaded into process %lu, %ls",
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

    const std::lock_guard<std::mutex> lock(g_pendingMutex);
    if (g_pending.count < kMaxLines) {
        Push(false, line);
    }
}

bool StartLayerLog() {
    if (g_started) {
        return true;
    }
    SetCoreLog(Discard);  // never stdout: the game owns it
    SetCoreLogError(Discard);

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

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset; the writer's until the game exits
    if (!g_wake) {
        return false;
    }

    // runs on its own
    const UniqueHandle thread(CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr));
    if (!thread) {
        return false;
    }

    g_started = true;
    SetCoreLog(AddLine);
    SetCoreLogError(AddFailure);
    return true;
}

void FollowReaderDebug(bool there, bool debug) {
    if (!there) {
        return;  // the last reader's word stands: the end of a session it read goes the same way
    }

    g_mode.store(debug ? kWrite : kDrop);
    SetEvent(g_wake);  // the held lines go now
}

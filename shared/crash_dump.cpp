// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/crash_dump.h"

#include <windows.h>
#include <dbghelp.h>

#include <cwchar>

#include "shared/local_files.h"
#include "shared/shared_memory.h"
#include "shared/unique_handle.h"

namespace {

    constexpr LONG kDumpsPerSession = 2;

    const wchar_t* g_plugin = L"plugin";  // set once by InstallCrashDump, before the handler is added

    LONG CALLBACK Handler(EXCEPTION_POINTERS* e) {
        const DWORD code = e->ExceptionRecord->ExceptionCode;
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION
            && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION
            && code != 0xC0000374 /* heap */) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // With both plugins installed, both handlers see the crash: the first one writes the dump.
        static LONG seen = 0;
        if (InterlockedIncrement(&seen) > kDumpsPerSession) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (InterlockedIncrement(&TheSharedMemory()->crashDumps) > kDumpsPerSession) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        wchar_t path[MAX_PATH];

        if (!NewLocalFilePath(LocalFileKind::kCrashDump, path, MAX_PATH)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const size_t used = wcslen(path);
        swprintf(path + used, MAX_PATH - used, L"_%08lx_%s.dmp", code, g_plugin);

        UniqueHandle f(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
        if (f) {
            MINIDUMP_EXCEPTION_INFORMATION info = {GetCurrentThreadId(), e, FALSE};
            MiniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                f.Get(),
                MINIDUMP_TYPE(
                    MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo
                    | MiniDumpWithUnloadedModules
                ),
                &info,
                nullptr,
                nullptr
            );
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

}

int InstallCrashDump(const wchar_t* plugin) {
    g_plugin = plugin;
    AddVectoredExceptionHandler(0, Handler);
    return PruneLocalFiles(LocalFileKind::kCrashDump, kDumpsPerSession);
}

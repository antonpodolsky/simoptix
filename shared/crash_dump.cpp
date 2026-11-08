// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/crash_dump.h"

#include <windows.h>
#include <dbghelp.h>

#include <cwchar>

#include "shared/shared_memory.h"
#include "shared/unique_handle.h"

namespace {

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
        if (InterlockedIncrement(&seen) > 2) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (InterlockedIncrement(&TheSharedMemory()->crashDumps) > 2) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        wchar_t dir[MAX_PATH], path[MAX_PATH];
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\simoptix", dir, MAX_PATH);
        CreateDirectoryW(dir, nullptr);
        SYSTEMTIME t;
        GetLocalTime(&t);
        swprintf(
            path,
            MAX_PATH,
            L"%s\\obs_%04d%02d%02d_%02d%02d%02d_%08lx_%s.dmp",
            dir,
            t.wYear,
            t.wMonth,
            t.wDay,
            t.wHour,
            t.wMinute,
            t.wSecond,
            code,
            g_plugin
        );

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

void InstallCrashDump(const wchar_t* plugin) {
    g_plugin = plugin;
    AddVectoredExceptionHandler(0, Handler);
}

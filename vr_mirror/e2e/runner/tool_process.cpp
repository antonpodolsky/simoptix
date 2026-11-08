// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tool_process.h"

namespace {

    HWND FindToolWindow(DWORD pid) {
        struct Ctx {
            DWORD pid;
            HWND found;
        } ctx = {pid, nullptr};

        EnumWindows(
            [](HWND h, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                DWORD p = 0;
                GetWindowThreadProcessId(h, &p);
                wchar_t cls[64];
                GetClassNameW(h, cls, 64);
                if (p == c->pid && wcscmp(cls, L"SimOptixVrMirror") == 0 && IsWindowVisible(h)) {
                    c->found = h;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx)
        );

        return ctx.found;
    }

}

bool Launch(const fs::path& dir, const fs::path& exe, const fs::path& scenarioFile, ToolProcess* out) {
    // Environment: inherit, plus the fake runtime's settings and the test hook.
    std::wstring env;
    for (wchar_t* e = GetEnvironmentStringsW(); *e; e += wcslen(e) + 1) {
        env += std::wstring(e) + L'\0';
    }
    env += L"SIMOPTIX_FAKE_PVR_SCENARIO=" + scenarioFile.wstring() + L'\0';
    env += L"SIMOPTIX_VR_MIRROR_SERVER_EXE=" + exe.filename().wstring() + L'\0';
    env += L"SIMOPTIX_VR_MIRROR_TRACE=" + (dir / L"tool_frames.csv").wstring() + L'\0';
    env += L'\0';

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    UniqueHandle logFile(CreateFileW(
        (dir / L"tool.log").c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = logFile.Get();
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    BOOL ok = CreateProcessW(
        exe.c_str(),
        cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        env.data(),
        dir.c_str(),
        &si,
        &out->pi
    );

    logFile.Reset();  // the child has its own copy
    if (!ok) {
        return false;
    }
    for (int i = 0; i < 100 && !out->hwnd; i++) {
        if (WaitForSingleObject(out->pi.hProcess, 100) == WAIT_OBJECT_0) {
            return false;
        }
        out->hwnd = FindToolWindow(out->pi.dwProcessId);
    }

    return out->hwnd != nullptr;
}

bool LaunchFakeGame(
    const fs::path& dir,
    const fs::path& exe,
    const fs::path& scenarioFile,
    const std::wstring& stopEvent,
    FakeGameProcess* out
) {
    out->stop.Reset(CreateEventW(nullptr, TRUE, FALSE, stopEvent.c_str()));
    if (!out->stop) {
        return false;
    }
    std::wstring env;
    for (wchar_t* e = GetEnvironmentStringsW(); *e; e += wcslen(e) + 1) {
        env += std::wstring(e) + L'\0';
    }
    env += L"SIMOPTIX_FAKE_OPENXR_SCENARIO=" + scenarioFile.wstring() + L'\0';
    env += L'\0';
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    UniqueHandle logFile(CreateFileW(
        (dir / L"fake_openxr.out").c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = logFile.Get();
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    const BOOL ok = CreateProcessW(
        exe.c_str(),
        cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        env.data(),
        dir.c_str(),
        &si,
        &out->pi
    );

    return ok != FALSE;
}

void StopFakeGame(FakeGameProcess& p) {
    if (p.stop) {
        SetEvent(p.stop.Get());
    }
    if (p.pi.hProcess) {
        if (WaitForSingleObject(p.pi.hProcess, 10000) != WAIT_OBJECT_0) {
            TerminateProcess(p.pi.hProcess, 1);
        }
        CloseHandle(p.pi.hProcess);
        CloseHandle(p.pi.hThread);
    }

    p = {};
}

void Close(ToolProcess& p) {
    if (p.hwnd) {
        PostMessageW(p.hwnd, WM_CLOSE, 0, 0);
    }
    if (WaitForSingleObject(p.pi.hProcess, 8000) != WAIT_OBJECT_0) {
        TerminateProcess(p.pi.hProcess, 1);
    }
    CloseHandle(p.pi.hProcess);
    CloseHandle(p.pi.hThread);
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_uninstall.h"

#include <string>

#include "setup_install.h"
#include "setup_places.h"
#include "setup_probe.h"
#include "setup_status.h"
#include "setup_system.h"
#include "shared/unique_handle.h"

namespace {

    constexpr DWORD kWaitForInstalledCopyMs = 10000;

    void ShowMessage(const std::wstring& text, UINT icon) {
        MessageBoxW(nullptr, text.c_str(), L"SimOptix Setup", MB_OK | icon);
    }

    // Copies setup into a folder of its own in the temporary folder, and returns the copy's path.
    bool CopyToTemporaryFolder(std::wstring* copy, std::wstring* error) {
        wchar_t temporaryRoot[MAX_PATH + 1] = {};

        if (GetTempPathW(static_cast<DWORD>(std::size(temporaryRoot)), temporaryRoot) == 0) {
            *error = L"Can't find the temporary folder: " + DescribeLastError();
            return false;
        }

        const std::wstring processId = std::to_wstring(GetCurrentProcessId());
        const std::wstring folder = std::wstring(temporaryRoot) + L"simoptix_uninstall_" + processId;
        *copy = folder + L"\\simoptix_setup.exe";

        if (!CreateFolders(folder, error)) {
            return false;
        }

        if (!CopyFileW(OwnExePath().c_str(), copy->c_str(), FALSE)) {
            *error = L"Can't copy setup to " + *copy + L": " + DescribeLastError();
            return false;
        }

        return true;
    }

    // Starts the copy with /uninstall, telling it to wait for this process to end first. It starts in its
    // own folder: a program's working folder can't be removed, and this one's may be setup's.
    bool StartUninstall(const std::wstring& copy, std::wstring* error) {
        std::wstring commandLine = L"\"" + copy + L"\" /uninstall /wait "
                                   + std::to_wstring(GetCurrentProcessId());
        const std::wstring folder = copy.substr(0, copy.find_last_of(L'\\'));
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};

        const BOOL started = CreateProcessW(
            copy.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            folder.c_str(),
            &startup,
            &process
        );

        if (!started) {
            *error = L"Can't start " + copy + L": " + DescribeLastError();
            return false;
        }

        // The copy runs on its own: its handles are only closed.
        const UniqueHandle processHandle(process.hProcess);
        const UniqueHandle threadHandle(process.hThread);

        return true;
    }

    // Waits up to 10 s. After that, the removal tries anyway, and reports a file still in use.
    void WaitForProcess(DWORD id) {
        const UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, id));

        if (process) {
            (void)WaitForSingleObject(process.Get(), kWaitForInstalledCopyMs);
        }
    }

    // A copy in the temporary folder goes, with its folder, when Windows next starts.
    void RemoveSelfAtRestart() {
        const std::wstring own = OwnExePath();
        const std::wstring folder = own.substr(0, own.find_last_of(L'\\'));

        (void)MoveFileExW(own.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);     // best effort
        (void)MoveFileExW(folder.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);  // after the file
    }

    std::wstring RemoveEverything(const Facts& facts) {
        std::wstring errors;

        for (const Part& part : kParts) {
            const PartFacts& partFacts = facts.parts[static_cast<size_t>(part.id)];
            const PartStatus status = DescribePart(part.id, partFacts, facts.system);
            std::wstring error;

            if (status.hasSomething && !RemovePart(part.id, &error)) {
                errors += std::wstring(L"\n") + part.shownName + L": " + error;
            }
        }

        return errors;
    }

}

int RunUninstall(DWORD waitFor) {
    if (EqualsNoCase(OwnExePath(), InstalledSetupPath())) {
        std::wstring copy;
        std::wstring error;

        if (!CopyToTemporaryFolder(&copy, &error) || !StartUninstall(copy, &error)) {
            ShowMessage(L"SimOptix could not be removed.\n\n" + error, MB_ICONERROR);
            return 1;
        }

        return 0;
    }

    if (waitFor != 0) {
        WaitForProcess(waitFor);
    }

    const Facts facts = ReadFacts(SignatureKind::kNone);

    if (facts.system.obsRunning || facts.system.lmuRunning) {
        const wchar_t* message = L"Close OBS Studio and Le Mans Ultimate, then uninstall SimOptix again. "
                                 L"They hold its files.";
        ShowMessage(message, MB_ICONWARNING);
        return 1;
    }

    std::wstring errors = RemoveEverything(facts);
    std::wstring entryError;

    // A failed removal keeps the entry, so that Uninstall can be tried again.
    if (errors.empty() && !UpdateAppEntry(false, &entryError)) {
        errors += L"\nWindows' list of installed apps: " + entryError;
    }

    if (waitFor != 0) {
        RemoveSelfAtRestart();
    }

    if (!errors.empty()) {
        ShowMessage(L"SimOptix could not be removed completely.\n" + errors, MB_ICONERROR);
        return 1;
    }

    ShowMessage(L"SimOptix was removed: VR Mirror, Overlays and the OpenXR layer.", MB_ICONINFORMATION);

    return 0;
}

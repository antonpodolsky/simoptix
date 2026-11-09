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

    // Copies setup to the temporary folder and starts it there with /uninstall.
    bool StartTemporaryCopy(std::wstring* error) {
        wchar_t temporaryRoot[MAX_PATH + 1] = {};

        if (GetTempPathW(static_cast<DWORD>(std::size(temporaryRoot)), temporaryRoot) == 0) {
            *error = L"Can't find the temporary folder: " + DescribeLastError();
            return false;
        }

        const std::wstring folder = std::wstring(temporaryRoot) + L"simoptix_uninstall_"
                                    + std::to_wstring(GetCurrentProcessId());
        const std::wstring copy = folder + L"\\simoptix_setup.exe";

        if (!CreateFolders(folder, error)) {
            return false;
        }

        if (!CopyFileW(OwnExePath().c_str(), copy.c_str(), FALSE)) {
            *error = L"Can't copy setup to " + copy + L": " + DescribeLastError();
            return false;
        }

        std::wstring commandLine = L"\"" + copy + L"\" /uninstall /wait "
                                   + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};

        if (!CreateProcessW(
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
            )) {
            *error = L"Can't start " + copy + L": " + DescribeLastError();
            return false;
        }

        const UniqueHandle processHandle(process.hProcess);
        const UniqueHandle threadHandle(process.hThread);
        return true;
    }

    void WaitForProcess(DWORD id) {
        const UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, id));

        if (process) {
            (void)WaitForSingleObject(
                process.Get(),
                kWaitForInstalledCopyMs
            );  // after that, removal tries anyway
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
            const PartStatus status = DescribePart(
                part.id,
                facts.parts[static_cast<size_t>(part.id)],
                facts.system
            );
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
        std::wstring error;

        if (!StartTemporaryCopy(&error)) {
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
        ShowMessage(
            L"Close OBS Studio and Le Mans Ultimate, then uninstall SimOptix again. They hold its files.",
            MB_ICONWARNING
        );
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

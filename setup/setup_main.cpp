// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix Setup, simoptix_setup.exe: installs and removes VR Mirror, Overlays and the OpenXR layer, which
// it carries inside itself (setup.rc), and shows what is installed. It runs as administrator
// (setup.manifest), because the layer goes into C:\Program Files and HKEY_LOCAL_MACHINE.
// With /uninstall, as Windows' list of installed apps runs it, it removes everything (setup_uninstall.h).
// Otherwise it shows its window (setup_window.h).

#include <windows.h>
#include <shellapi.h>

#include <cwchar>

#include "setup_uninstall.h"
#include "setup_window.h"

namespace {

    struct Arguments {
        bool uninstall = false;
        DWORD waitFor = 0;  // /wait <process id>
    };

    Arguments ReadArguments() {
        Arguments arguments;
        int count = 0;
        wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);

        if (!values) {
            return arguments;
        }

        for (int i = 1; i < count; ++i) {
            if (_wcsicmp(values[i], L"/uninstall") == 0) {
                arguments.uninstall = true;
            } else if (_wcsicmp(values[i], L"/wait") == 0 && i + 1 < count) {
                arguments.waitFor = static_cast<DWORD>(wcstoul(values[++i], nullptr, 10));
            }
        }

        LocalFree(values);
        return arguments;
    }

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const Arguments arguments = ReadArguments();

    if (arguments.uninstall) {
        return RunUninstall(arguments.waitFor);
    }

    return RunSetupWindow(instance);
}

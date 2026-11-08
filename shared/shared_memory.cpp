// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/shared_memory.h"

#include <cwchar>

namespace {

    constexpr DWORD kBlockSize = 4096;  // one page: room for fields appended later

    SharedMemory* Open() {
        wchar_t name[64];
        swprintf(name, 64, L"Local\\simoptix_%lu", GetCurrentProcessId());

        // The handle is kept open for the life of the process, like the view.
        HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kBlockSize, name);
        if (!map) {
            return nullptr;
        }
        void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, kBlockSize);
        if (!view) {
            CloseHandle(map);
            return nullptr;
        }

        return static_cast<SharedMemory*>(view);
    }

}

SharedMemory* TheSharedMemory() {
    static SharedMemory own = {};
    static SharedMemory* const block = [] {
        SharedMemory* b = Open();
        return b ? b : &own;
    }();

    return block;
}

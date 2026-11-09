// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The registry calls of SimOptix Setup: reading and writing values under HKEY_LOCAL_MACHINE, always in
// its 64-bit view. Thin wrappers around Win32. Knows nothing about what the keys are for.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct RegistryDword {
    std::wstring name;
    DWORD value = 0;
};

// The DWORD values of the key. An absent key has none.
std::vector<RegistryDword> ReadDwordValues(const wchar_t* keyPath);

// Each makes the key when it isn't there.
bool WriteDwordValue(const wchar_t* keyPath, const std::wstring& name, DWORD value, std::wstring* error);
bool WriteStringValue(
    const wchar_t* keyPath,
    const std::wstring& name,
    const std::wstring& value,
    std::wstring* error
);

// A value or key that isn't there is no error.
bool DeleteValue(const wchar_t* keyPath, const std::wstring& name, std::wstring* error);
bool DeleteKey(const wchar_t* keyPath, std::wstring* error);

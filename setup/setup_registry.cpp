// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_registry.h"

namespace {

    // Owns an open registry key and closes it.
    class RegistryKey {
    public:
        RegistryKey() = default;
        RegistryKey(const RegistryKey&) = delete;
        RegistryKey& operator=(const RegistryKey&) = delete;
        ~RegistryKey() {
            if (key_) {
                RegCloseKey(key_);
            }
        }

        LSTATUS Open(const wchar_t* path, REGSAM access) {
            return RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access | KEY_WOW64_64KEY, &key_);
        }

        LSTATUS Create(const wchar_t* path) {
            return RegCreateKeyExW(
                HKEY_LOCAL_MACHINE,
                path,
                0,
                nullptr,
                0,
                KEY_SET_VALUE | KEY_WOW64_64KEY,
                nullptr,
                &key_,
                nullptr
            );
        }

        HKEY Get() const { return key_; }

    private:
        HKEY key_ = nullptr;
    };

    std::wstring DescribeStatus(const wchar_t* what, const wchar_t* keyPath, LSTATUS status) {
        return std::wstring(L"Can't ") + what + L" HKEY_LOCAL_MACHINE\\" + keyPath + L" (error "
               + std::to_wstring(status) + L").";
    }

}

std::vector<RegistryDword> ReadDwordValues(const wchar_t* keyPath) {
    std::vector<RegistryDword> values;
    RegistryKey key;

    if (key.Open(keyPath, KEY_QUERY_VALUE) != ERROR_SUCCESS) {
        return values;
    }

    // Value names here are full paths, which fit in 32767 characters (the registry's own limit).
    std::wstring name(32768, L'\0');

    for (DWORD index = 0;; ++index) {
        DWORD nameLength = static_cast<DWORD>(name.size());
        DWORD type = 0;
        DWORD value = 0;
        DWORD valueSize = sizeof(value);

        const LSTATUS status = RegEnumValueW(
            key.Get(),
            index,
            name.data(),
            &nameLength,
            nullptr,
            &type,
            reinterpret_cast<BYTE*>(&value),
            &valueSize
        );

        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (status == ERROR_SUCCESS && type == REG_DWORD) {
            values.push_back(RegistryDword{name.substr(0, nameLength), value});
        }
    }

    return values;
}

bool WriteDwordValue(const wchar_t* keyPath, const std::wstring& name, DWORD value, std::wstring* error) {
    RegistryKey key;
    LSTATUS status = key.Create(keyPath);

    if (status == ERROR_SUCCESS) {
        status = RegSetValueExW(
            key.Get(),
            name.c_str(),
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value)
        );
    }

    if (status != ERROR_SUCCESS) {
        *error = DescribeStatus(L"write to", keyPath, status);
        return false;
    }

    return true;
}

bool WriteStringValue(
    const wchar_t* keyPath,
    const std::wstring& name,
    const std::wstring& value,
    std::wstring* error
) {
    RegistryKey key;
    LSTATUS status = key.Create(keyPath);

    if (status == ERROR_SUCCESS) {
        status = RegSetValueExW(
            key.Get(),
            name.c_str(),
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))
        );
    }

    if (status != ERROR_SUCCESS) {
        *error = DescribeStatus(L"write to", keyPath, status);
        return false;
    }

    return true;
}

bool DeleteValue(const wchar_t* keyPath, const std::wstring& name, std::wstring* error) {
    RegistryKey key;
    LSTATUS status = key.Open(keyPath, KEY_SET_VALUE);

    if (status == ERROR_FILE_NOT_FOUND) {
        return true;
    }

    if (status == ERROR_SUCCESS) {
        status = RegDeleteValueW(key.Get(), name.c_str());
    }

    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        *error = DescribeStatus(L"change", keyPath, status);
        return false;
    }

    return true;
}

bool DeleteKey(const wchar_t* keyPath, std::wstring* error) {
    const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, keyPath);

    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        *error = DescribeStatus(L"remove", keyPath, status);
        return false;
    }

    return true;
}

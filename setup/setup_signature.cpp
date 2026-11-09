// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_signature.h"

#include <windows.h>
#include <softpub.h>
#include <wintrust.h>

#include "setup_system.h"

namespace {

    bool IsUnsigned(LONG result) {
        return result == TRUST_E_NOSIGNATURE || result == TRUST_E_SUBJECT_FORM_UNKNOWN
               || result == TRUST_E_PROVIDER_UNKNOWN;
    }

}

SignatureKind CheckSignature(const std::wstring& path) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    // Revocation isn't checked, so the check needs no network and gives the same answer offline.
    WINTRUST_DATA data = {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);

    data.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);  // only frees the state

    if (result == ERROR_SUCCESS) {
        return SignatureKind::kTrusted;
    }

    return IsUnsigned(result) ? SignatureKind::kNone : SignatureKind::kUntrusted;
}

SignatureKind CheckEmbeddedSignature(const EmbeddedFile& file, const wchar_t* fileName) {
    wchar_t temporaryRoot[MAX_PATH + 1] = {};

    if (GetTempPathW(static_cast<DWORD>(std::size(temporaryRoot)), temporaryRoot) == 0) {
        return SignatureKind::kNone;
    }

    const std::wstring folder = std::wstring(temporaryRoot) + L"simoptix_setup_"
                                + std::to_wstring(GetCurrentProcessId());
    const std::wstring path = folder + L"\\" + fileName;
    std::wstring error;
    SignatureKind signature = SignatureKind::kNone;

    if (CreateFolders(folder, &error) && WriteEmbeddedFile(file, path, &error)) {
        signature = CheckSignature(path);
    }

    (void)RemoveFolderTree(folder, &error);  // a leftover in the temporary folder harms nothing
    return signature;
}

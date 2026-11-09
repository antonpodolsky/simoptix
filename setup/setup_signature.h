// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Reads a file's code signature with Windows' own check (WinVerifyTrust): none, trusted, or signed with a
// certificate Windows doesn't trust. Knows nothing about which file it checks.
#pragma once

#include <string>

#include "setup_facts.h"
#include "setup_payload.h"

// A file that can't be read counts as not signed.
SignatureKind CheckSignature(const std::wstring& path);

// The same for a file inside setup's exe: it is written to a temporary folder for the check, because
// Windows checks a DLL's signature only as a file.
SignatureKind CheckEmbeddedSignature(const EmbeddedFile& file, const wchar_t* fileName);

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The files SimOptix Setup carries inside its exe, as resources (setup.rc), and writing them to disk or
// comparing them with a file on disk. Knows nothing about which part a file belongs to or where it goes.
#pragma once

#include <cstddef>
#include <string>

// A file inside the exe. The bytes stay mapped for as long as the exe runs.
struct EmbeddedFile {
    const void* data = nullptr;
    size_t size = 0;
};

bool FindEmbeddedFile(const wchar_t* resource, EmbeddedFile* file, std::wstring* error);

// True when the file at `path` holds exactly the embedded file's bytes. False when it differs or can't
// be read.
bool HasSameBytes(const std::wstring& path, const EmbeddedFile& file);

// Writes the embedded file to `path` through a temporary file beside it, so that a failed write leaves
// the old file as it was.
bool WriteEmbeddedFile(const EmbeddedFile& file, const std::wstring& path, std::wstring* error);

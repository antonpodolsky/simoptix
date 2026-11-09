// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The local files test's view of a test %LOCALAPPDATA%: making files that stand in for older ones, and
// reading what is left. A name made here has the form the plugins and the layer give their files
// (<name>_<yyyymmdd>_<hhmmss>...), with times on 2026-01-01 that sort by their number. Knows nothing about
// which program writes what.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// "<name>_20260101_0000<i>": the stem of an older file, number `i` (0 the oldest).
std::wstring SeedStem(const wchar_t* name, int i);

// Makes `path` with `text`.
bool Touch(const fs::path& path, const char* text = "older\n");

// The file names in `dir` that start with `name`, a '_', a time and then '_' or '.', and end with
// `extension`, sorted. Empty if `dir` is missing.
std::vector<std::wstring> TimedFiles(const fs::path& dir, const wchar_t* name, const wchar_t* extension);

// The distinct "<name>_<yyyymmdd>_<hhmmss>" stems of `files`, sorted.
std::vector<std::wstring> StemsOf(const std::vector<std::wstring>& files, const wchar_t* name);

// The lines of a text file. Empty if it is missing.
std::vector<std::string> Lines(const fs::path& path);

bool Exists(const fs::path& path);

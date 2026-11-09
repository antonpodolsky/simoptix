// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_files_folder.h"

#include <algorithm>
#include <cwchar>
#include <fstream>
#include <regex>

std::wstring SeedStem(const wchar_t* name, int i) {
    wchar_t stem[128];
    swprintf(stem, 128, L"%s_20260101_%06d", name, i);
    return stem;
}

bool Touch(const fs::path& path, const char* text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    return file.good();
}

std::vector<std::wstring> TimedFiles(const fs::path& dir, const wchar_t* name, const wchar_t* extension) {
    std::vector<std::wstring> files;
    std::error_code ec;

    if (!fs::is_directory(dir, ec)) {
        return files;
    }

    const std::wregex timed(std::wstring(name) + L"_\\d{8}_\\d{6}([_.].*)?");

    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        const std::wstring file = entry.path().filename().wstring();
        const size_t extensionChars = wcslen(extension);
        const bool endsRight = file.size() > extensionChars
                               && _wcsicmp(file.c_str() + file.size() - extensionChars, extension) == 0;

        if (entry.is_regular_file() && endsRight && std::regex_match(file, timed)) {
            files.push_back(file);
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::wstring> StemsOf(const std::vector<std::wstring>& files, const wchar_t* name) {
    const size_t stemChars = wcslen(name) + 1 + 15;
    std::vector<std::wstring> stems;

    for (const std::wstring& file : files) {
        const std::wstring stem = file.substr(0, stemChars);

        if (stems.empty() || stems.back() != stem) {
            stems.push_back(stem);
        }
    }

    return stems;
}

std::vector<std::string> Lines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    return lines;
}

bool Exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

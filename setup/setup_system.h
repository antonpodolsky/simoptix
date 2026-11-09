// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// What SimOptix Setup asks Windows: the folders it installs to, OBS Studio's place and version, whether a
// program runs, and folder work. Thin wrappers around Win32. Knows nothing about the parts.
#pragma once

#include <string>

// "Access is denied. (5)": Windows' text for GetLastError().
std::wstring DescribeLastError();

std::wstring ProgramFilesFolder();  // C:\Program Files
std::wstring ProgramDataFolder();   // C:\ProgramData

// The full path of the running exe.
std::wstring OwnExePath();

struct ObsInstall {
    bool found = false;
    int major = 0;  // 32 for OBS 32.2.2
};

// OBS Studio as its installer registered it (HKLM\SOFTWARE\OBS Studio), with the major version of its
// bin\64bit\obs64.exe.
ObsInstall FindObs();

// The ProductVersion text of the file's version resource ("0.1.0+14.g31637d5"), empty when it has
// none.
std::wstring ReadProductVersion(const std::wstring& path);

// Whether a process with this file name ("obs64.exe") runs, in any session.
bool IsProcessRunning(const wchar_t* exeName);

bool CreateFolders(const std::wstring& path, std::wstring* error);

// Removes the folder with everything in it. A folder that isn't there is no error.
bool RemoveFolderTree(const std::wstring& path, std::wstring* error);

// Removes the folder when it is empty, and leaves it otherwise.
void RemoveFolderIfEmpty(const std::wstring& path);

bool FileExists(const std::wstring& path);

// Paths and file names compare without case, as Windows does.
bool EqualsNoCase(const std::wstring& text, const std::wstring& other);

// Whether `text` ends with `ending`, without case.
bool EndsWithNoCase(const std::wstring& text, const std::wstring& ending);

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_install.h"

#include <windows.h>

#include "setup_payload.h"
#include "setup_places.h"
#include "setup_probe.h"
#include "setup_registry.h"
#include "setup_system.h"
#include "setup_version.h"

namespace {

    bool WritePartFile(const wchar_t* resource, const std::wstring& path, std::wstring* error) {
        EmbeddedFile file;

        if (!FindEmbeddedFile(resource, &file, error)) {
            return false;
        }

        return WriteEmbeddedFile(file, path, error);
    }

    bool InstallPlugin(const Part& part, const SystemFacts& system, std::wstring* error) {
        const std::wstring folder = PluginFolder(part);
        const bool flat = system.obsLayout == PluginLayout::kFlat;
        const std::wstring dllFolder = flat ? folder : folder + L"\\bin\\64bit";

        if (!CreateFolders(dllFolder, error) || !CreateFolders(folder + L"\\data", error)) {
            return false;
        }

        // The DLL goes first, so a manifest is never there without its DLL.
        if (!WritePartFile(part.dllResource, PluginDllPath(part, system.obsLayout), error)
            || !WritePartFile(part.manifestResource, PluginManifestPath(part), error)) {
            return false;
        }

        if (flat) {
            return RemoveFolderTree(folder + L"\\bin", error);
        }

        const std::wstring flatDll = PluginDllPath(part, PluginLayout::kFlat);

        if (FileExists(flatDll) && !DeleteFileW(flatDll.c_str())) {
            *error = L"Can't remove " + flatDll + L": " + DescribeLastError();
            return false;
        }

        return true;
    }

    bool UnregisterLayers(const std::wstring& except, std::wstring* error) {
        for (const std::wstring& manifest : FindLayerRegistrations()) {
            if (!EqualsNoCase(manifest, except) && !DeleteValue(kLayerRegistryKey, manifest, error)) {
                return false;
            }
        }

        return true;
    }

    bool InstallLayer(const Part& part, std::wstring* error) {
        const std::wstring manifest = LayerManifestPath();

        if (!CreateFolders(LayerFolder(), error)) {
            return false;
        }

        // The DLL goes first, so a manifest is never there without its DLL.
        if (!WritePartFile(part.dllResource, LayerDllPath(), error)
            || !WritePartFile(part.manifestResource, manifest, error)) {
            return false;
        }

        if (!WriteDwordValue(kLayerRegistryKey, manifest, 0, error)) {
            return false;
        }

        return UnregisterLayers(manifest, error);
    }

    bool RemoveLayer(std::wstring* error) {
        if (!UnregisterLayers(L"", error)) {
            return false;
        }

        if (!RemoveFolderTree(LayerFolder(), error)) {
            return false;
        }

        RemoveFolderIfEmpty(SimOptixFolder());
        return true;
    }

    bool CopySetupToProgramFiles(std::wstring* error) {
        const std::wstring own = OwnExePath();
        const std::wstring installed = InstalledSetupPath();

        if (EqualsNoCase(own, installed)) {
            return true;
        }

        if (!CreateFolders(SimOptixFolder(), error)) {
            return false;
        }

        if (!CopyFileW(own.c_str(), installed.c_str(), FALSE)) {
            *error = L"Can't copy setup to " + installed + L": " + DescribeLastError();
            return false;
        }

        return true;
    }

    bool WriteAppEntry(std::wstring* error) {
        const std::wstring installed = InstalledSetupPath();
        const std::wstring quoted = L"\"" + installed + L"\"";

        const struct {
            const wchar_t* name;
            std::wstring value;
        } values[] = {
            {L"DisplayName", L"SimOptix"},
            {L"DisplayVersion", SetupVersion()},
            {L"Publisher", L"Anton Podolsky"},
            {L"DisplayIcon", installed},
            {L"InstallLocation", SimOptixFolder()},
            {L"URLInfoAbout", L"https://github.com/antonpodolsky/simoptix"},
            {L"ModifyPath", quoted},
            {L"UninstallString", quoted + L" /uninstall"},
        };

        for (const auto& value : values) {
            if (!WriteStringValue(kAppEntryKey, value.name, value.value, error)) {
                return false;
            }
        }

        return WriteDwordValue(kAppEntryKey, L"NoRepair", 1, error);
    }

    // The running exe can't delete itself, so when it is the installed copy Windows deletes it at the next
    // start.
    void RemoveInstalledSetup() {
        const std::wstring installed = InstalledSetupPath();

        if (!FileExists(installed)) {
            return;
        }

        if (EqualsNoCase(OwnExePath(), installed) || !DeleteFileW(installed.c_str())) {
            (void)MoveFileExW(installed.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);  // best effort
            return;
        }

        RemoveFolderIfEmpty(SimOptixFolder());
    }

}

bool InstallPart(PartId id, const SystemFacts& system, std::wstring* error) {
    const Part& part = PartOf(id);

    if (IsPlugin(id)) {
        return InstallPlugin(part, system, error);
    }

    return InstallLayer(part, error);
}

bool RemovePart(PartId id, std::wstring* error) {
    if (IsPlugin(id)) {
        return RemoveFolderTree(PluginFolder(PartOf(id)), error);
    }

    return RemoveLayer(error);
}

bool UpdateAppEntry(bool anythingInstalled, std::wstring* error) {
    if (anythingInstalled) {
        return CopySetupToProgramFiles(error) && WriteAppEntry(error);
    }

    if (!DeleteKey(kAppEntryKey, error)) {
        return false;
    }

    RemoveInstalledSetup();
    return true;
}

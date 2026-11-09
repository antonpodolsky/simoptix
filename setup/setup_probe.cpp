// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_probe.h"

#include "setup_parts.h"
#include "setup_payload.h"
#include "setup_places.h"
#include "setup_registry.h"
#include "setup_status.h"
#include "setup_system.h"

namespace {

    bool IsSameAsEmbedded(const std::wstring& path, const wchar_t* resource) {
        EmbeddedFile file;
        std::wstring error;
        return FindEmbeddedFile(resource, &file, &error) && HasSameBytes(path, file);
    }

    // The part's DLL decides whether it is installed. It is the same build only when its manifest is
    // the same too, so a copy with a missing or older manifest counts as different.
    InstalledCopy
    CompareCopy(const Part& part, const std::wstring& dllPath, const std::wstring& manifestPath) {
        if (!FileExists(dllPath)) {
            return InstalledCopy::kNone;
        }

        if (!IsSameAsEmbedded(dllPath, part.dllResource)
            || !IsSameAsEmbedded(manifestPath, part.manifestResource)) {
            return InstalledCopy::kDifferent;
        }

        return InstalledCopy::kSame;
    }

    PluginLayout OtherLayout(PluginLayout layout) {
        return layout == PluginLayout::kFlat ? PluginLayout::kBin64bit : PluginLayout::kFlat;
    }

    PartFacts ReadPluginFacts(const Part& part, const SystemFacts& system) {
        PartFacts facts;
        const std::wstring manifest = PluginManifestPath(part);

        // Without OBS there is no layout to prefer, so a copy in either one counts as installed.
        if (system.obsFound) {
            const std::wstring dll = PluginDllPath(part, system.obsLayout);
            facts.copy = CompareCopy(part, dll, manifest);
            facts.installedVersion = ReadProductVersion(dll);
            facts.inOtherLayout = FileExists(PluginDllPath(part, OtherLayout(system.obsLayout)));
            return facts;
        }

        for (const PluginLayout layout : {PluginLayout::kBin64bit, PluginLayout::kFlat}) {
            const std::wstring dll = PluginDllPath(part, layout);
            const InstalledCopy copy = CompareCopy(part, dll, manifest);

            if (copy != InstalledCopy::kNone) {
                facts.copy = copy;
                facts.installedVersion = ReadProductVersion(dll);
            }
        }

        return facts;
    }

    PartFacts ReadLayerFacts(const Part& part) {
        PartFacts facts;
        const std::wstring manifest = LayerManifestPath();

        facts.copy = CompareCopy(part, LayerDllPath(), manifest);
        facts.installedVersion = ReadProductVersion(LayerDllPath());

        for (const RegistryDword& value : ReadDwordValues(kLayerRegistryKey)) {
            if (EqualsNoCase(value.name, manifest)) {
                facts.registered = true;
                facts.switchedOn = value.value == 0;
            } else if (EndsWithNoCase(value.name, std::wstring(L"\\") + part.manifestName)) {
                facts.otherRegistrations.push_back(value.name);
            }
        }

        return facts;
    }

    SystemFacts ReadSystemFacts(SignatureKind setupLayerSignature) {
        SystemFacts system;
        const ObsInstall obs = FindObs();

        system.obsFound = obs.found;
        system.obsLayout = LayoutOfObs(obs.major);
        system.obsRunning = IsProcessRunning(L"obs64.exe");
        system.lmuRunning = IsProcessRunning(L"Le Mans Ultimate.exe");
        system.setupLayerSignature = setupLayerSignature;
        return system;
    }

}

Facts ReadFacts(SignatureKind setupLayerSignature) {
    Facts facts;
    facts.system = ReadSystemFacts(setupLayerSignature);

    for (const Part& part : kParts) {
        PartFacts& partFacts = facts.parts[static_cast<size_t>(part.id)];
        partFacts = IsPlugin(part.id) ? ReadPluginFacts(part, facts.system) : ReadLayerFacts(part);
    }

    return facts;
}

std::vector<std::wstring> FindLayerRegistrations() {
    std::vector<std::wstring> paths;

    for (const RegistryDword& value : ReadDwordValues(kLayerRegistryKey)) {
        if (EndsWithNoCase(value.name, std::wstring(L"\\") + PartOf(PartId::kOpenXrLayer).manifestName)) {
            paths.push_back(value.name);
        }
    }

    return paths;
}

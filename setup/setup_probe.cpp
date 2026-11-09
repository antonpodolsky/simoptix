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

    // Every file that makes up the part is compared, so a copy whose manifest differs counts as different.
    InstalledCopy CompareCopy(const std::wstring& path, const wchar_t* resource) {
        if (!FileExists(path)) {
            return InstalledCopy::kNone;
        }

        EmbeddedFile file;
        std::wstring error;

        if (!FindEmbeddedFile(resource, &file, &error) || !HasSameBytes(path, file)) {
            return InstalledCopy::kDifferent;
        }

        return InstalledCopy::kSame;
    }

    PluginLayout OtherLayout(PluginLayout layout) {
        return layout == PluginLayout::kFlat ? PluginLayout::kBin64bit : PluginLayout::kFlat;
    }

    PartFacts ReadPluginFacts(const Part& part, const SystemFacts& system) {
        PartFacts facts;

        // Without OBS there is no layout to prefer, so a copy in either one counts as installed.
        if (system.obsFound) {
            facts.copy = CompareCopy(PluginDllPath(part, system.obsLayout), part.dllResource);
            facts.inOtherLayout = FileExists(PluginDllPath(part, OtherLayout(system.obsLayout)));
            return facts;
        }

        for (const PluginLayout layout : {PluginLayout::kBin64bit, PluginLayout::kFlat}) {
            const InstalledCopy copy = CompareCopy(PluginDllPath(part, layout), part.dllResource);

            if (copy != InstalledCopy::kNone) {
                facts.copy = copy;
            }
        }

        return facts;
    }

    PartFacts ReadLayerFacts() {
        PartFacts facts;
        const std::wstring manifest = LayerManifestPath();

        facts.copy = CompareCopy(LayerDllPath(), PartOf(PartId::kOpenXrLayer).dllResource);

        if (facts.copy == InstalledCopy::kSame
            && CompareCopy(manifest, kLayerManifestResource) != InstalledCopy::kSame) {
            facts.copy = InstalledCopy::kDifferent;
        }

        for (const RegistryDword& value : ReadDwordValues(kLayerRegistryKey)) {
            if (EqualsNoCase(value.name, manifest)) {
                facts.registered = true;
                facts.switchedOn = value.value == 0;
            } else if (EndsWithNoCase(value.name, std::wstring(L"\\") + kLayerManifestName)) {
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
        partFacts = IsPlugin(part.id) ? ReadPluginFacts(part, facts.system) : ReadLayerFacts();
    }

    return facts;
}

std::vector<std::wstring> FindLayerRegistrations() {
    std::vector<std::wstring> paths;

    for (const RegistryDword& value : ReadDwordValues(kLayerRegistryKey)) {
        if (EndsWithNoCase(value.name, std::wstring(L"\\") + kLayerManifestName)) {
            paths.push_back(value.name);
        }
    }

    return paths;
}

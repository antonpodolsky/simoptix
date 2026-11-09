// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_status.h"

namespace {

    constexpr int kFirstObsWithFlatLayout = 33;

    // From the installed copy alone: nothing there, a different build, or the same.
    PartAction ActionForCopy(InstalledCopy copy) {
        switch (copy) {
            case InstalledCopy::kNone:
                return PartAction::kInstall;
            case InstalledCopy::kDifferent:
                return PartAction::kUpdate;
            case InstalledCopy::kSame:
                break;
        }
        return PartAction::kNone;
    }

    PartStatus DescribePlugin(const PartFacts& part, const SystemFacts& system) {
        PartStatus status;
        status.hasSomething = part.copy != InstalledCopy::kNone || part.inOtherLayout;
        status.action = ActionForCopy(part.copy);

        if (part.inOtherLayout && status.action != PartAction::kUpdate) {
            status.action = PartAction::kRepair;
        }

        if (system.obsRunning) {
            status.installBlockedBy = L"Close OBS Studio to change the plugins.";
            status.removeBlockedBy = status.installBlockedBy;
        } else if (!system.obsFound) {
            status.installBlockedBy = L"OBS Studio was not found. Install it to install the plugins.";
        }

        return status;
    }

    PartStatus DescribeLayer(const PartFacts& part, const SystemFacts& system) {
        PartStatus status;
        status.hasSomething = part.copy != InstalledCopy::kNone || part.registered
                              || !part.otherRegistrations.empty();
        status.action = ActionForCopy(part.copy);

        const bool outOfPlace = !part.registered || !part.switchedOn || !part.otherRegistrations.empty();

        if (status.hasSomething && status.action != PartAction::kUpdate && outOfPlace) {
            status.action = PartAction::kRepair;
        }

        if (system.lmuRunning) {
            status.installBlockedBy = L"Close LMU to change the OpenXR layer.";
            status.removeBlockedBy = status.installBlockedBy;
        }

        return status;
    }

}

PluginLayout LayoutOfObs(int major) {
    return major >= kFirstObsWithFlatLayout ? PluginLayout::kFlat : PluginLayout::kBin64bit;
}

PartStatus DescribePart(PartId id, const PartFacts& part, const SystemFacts& system) {
    if (IsPlugin(id)) {
        return DescribePlugin(part, system);
    }

    return DescribeLayer(part, system);
}

const wchar_t* ActionLabel(PartAction action) {
    switch (action) {
        case PartAction::kInstall:
            return L"Install";
        case PartAction::kUpdate:
            return L"Update";
        case PartAction::kRepair:
            return L"Repair";
        case PartAction::kNone:
            break;
    }
    return L"";
}

std::wstring UnsignedLayerProblem(const SystemFacts& system) {
    switch (system.setupLayerSignature) {
        case SignatureKind::kTrusted:
            return L"";
        case SignatureKind::kUntrusted:
            return L"The OpenXR layer in this setup is signed with a certificate Windows doesn't trust.";
        case SignatureKind::kNone:
            break;
    }
    return L"The OpenXR layer in this setup is not signed.";
}

const wchar_t* UnsignedLayerConsequence() {
    return L"Easy Anti-Cheat refuses such a layer. While it is installed, LMU started the usual way shows "
           L"Easy Anti-Cheat's \"Untrusted system file\" warning, and online play is disabled. The layer "
           L"works in LMU started without Easy Anti-Cheat, which means offline.";
}

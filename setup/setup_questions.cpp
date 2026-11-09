// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_questions.h"

#include "setup_dialog.h"
#include "setup_status.h"

namespace {

    // Both questions put the risky answer first and the safe one, their default, second.
    constexpr size_t kGoOn = 0;
    constexpr size_t kStop = 1;

    // "the VR Mirror OBS plugin, the Overlays OBS plugin and the OpenXR layer"
    std::wstring ListParts(const std::vector<PartId>& parts) {
        std::wstring names;

        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                names += i + 1 == parts.size() ? L" and " : L", ";
            }

            names += std::wstring(L"the ") + PartOf(parts[i]).shownName;
        }

        return names;
    }

}

bool AskInstallRefusedLayer(HWND owner, const std::wstring& problem) {
    const DialogSpec question = {
        L"Install a layer Easy Anti-Cheat refuses?",
        {problem, UnsignedLayerConsequence()},
        {{
            {L"Install anyway", ButtonKind::kStandard},
            {L"Don't install", ButtonKind::kAccentFill},
        }},
        kStop,
    };

    return AskSetupQuestion(owner, question) == kGoOn;
}

bool AskUninstallAll(HWND owner, const std::vector<PartId>& parts) {
    const DialogSpec question = {
        L"Uninstall all of SimOptix?",
        {L"Setup removes " + ListParts(parts) + L" from this PC."},
        {{
            {L"Uninstall all", ButtonKind::kDangerFill},
            {L"Cancel", ButtonKind::kAccentFill},
        }},
        kStop,
    };

    return AskSetupQuestion(owner, question) == kGoOn;
}

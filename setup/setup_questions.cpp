// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_questions.h"

#include "setup_dialog.h"
#include "setup_status.h"

namespace {

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
    DialogSpec question;
    question.title = L"Install a layer Easy Anti-Cheat refuses?";
    question.paragraphs = {problem, UnsignedLayerConsequence()};
    question.buttons = {
        {{L"Install anyway", ButtonKind::kStandard}, {L"Don't install", ButtonKind::kAccentFill}}
    };
    question.defaultButton = 1;
    return AskSetupQuestion(owner, question) == 0;
}

bool AskUninstallAll(HWND owner, const std::vector<PartId>& parts) {
    DialogSpec question;
    question.title = L"Uninstall all of SimOptix?";
    question.paragraphs = {L"Setup removes " + ListParts(parts) + L" from this PC."};
    question.buttons = {{{L"Uninstall all", ButtonKind::kDangerFill}, {L"Cancel", ButtonKind::kAccentFill}}};
    question.defaultButton = 1;
    return AskSetupQuestion(owner, question) == 0;
}

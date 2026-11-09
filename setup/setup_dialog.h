// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix Setup's questions, in its own look: a small modal window with the band's colour along the
// top, a warning mark and the question, the reasons in the body, and two buttons in the footer. Closing
// it, or Escape, gives the default answer, which is always the safe one. Knows nothing about what is
// asked.
#pragma once

#include <windows.h>

#include <array>
#include <string>
#include <vector>

#include "setup_buttons.h"

struct DialogButton {
    std::wstring label;
    ButtonKind kind = ButtonKind::kStandard;
};

struct DialogSpec {
    std::wstring title;                    // the question, one line
    std::vector<std::wstring> paragraphs;  // the first in the main text colour, the rest in grey
    std::array<DialogButton, 2> buttons;   // left to right
    size_t defaultButton = 1;              // has the focus, and answers Enter, Escape and closing
};

// Shows the dialog over `owner` and returns the index of the button chosen.
size_t AskSetupQuestion(HWND owner, const DialogSpec& spec);

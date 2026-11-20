// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "empty_state_text.h"

namespace {

    // The messages the empty state shows under its rule in place of the facts. `steps`: the source's own
    // (EmptyStateTextInput::headsetSteps).
    EmptyMessage HeadsetUnreadMessage(const std::vector<std::wstring>* steps) {
        EmptyMessage message = {L"The headset didn't answer", FactTone::kWarning, {}, L""};

        if (steps) {
            message.steps = *steps;
        }

        message.steps.push_back(L"Close this window and open it again");
        return message;
    }

    EmptyMessage LapFailedMessage() {
        return {
            L"The last lap wasn't recorded",
            FactTone::kWarning,
            {L"Find why in the OBS log: lines starting \"recorded lap:\"",
             L"Press \"Record a lap\" to try again"},
            L""
        };
    }

    EmptyMessage RecordingMessage() {
        return {
            L"Recording a lap",
            FactTone::kPlain,
            {L"Start a practice session in LMU",
             L"Leave the pits and cross the start/finish line",
             L"Drive one full lap back to the start/finish line"},
            L"Entering the pits or pausing the game starts the lap over at the next start/finish line."
        };
    }

}

EmptyText EmptyStateText(const EmptyStateTextInput& in, const FrameSource& game) {
    EmptyText text;
    text.title = L"Waiting for the game";

    if (in.lapWanted) {
        text.lines = {L"Start LMU in VR."};
    } else if (in.lapRecorded) {
        text.lines = {L"Start LMU in VR, or press \"Play recorded lap\" below."};
    } else {
        text.lines = {L"Start LMU in VR, or record a lap with \"Record a lap\" below."};
    }

    if (in.setup.headsetUnread && in.setup.facts.imageWidth == 0) {
        text.message = HeadsetUnreadMessage(in.headsetSteps);
    } else if (in.lapFailed) {
        text.message = LapFailedMessage();
    } else if (in.lapWanted) {
        text.message = RecordingMessage();
    } else {
        text.facts = SetupFacts(in.setup, game);
    }

    return text;
}

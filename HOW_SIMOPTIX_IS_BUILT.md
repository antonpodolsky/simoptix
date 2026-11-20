# How SimOptix is built

## The dev workflow

The workflow is tailored to a wide type of coding agents.
- **The dev skill** (`.agents\skills\dev\SKILL.md`) holds the rules for the whole repo and the order
  to work in. The rules cover using only the game's official interfaces, a minimal footprint beside the
  game, and the code's layers and which way they depend. `AGENTS.md` points every coding agent at the
  skills.
- **The code practices skill** (`.agents\skills\code-practices\SKILL.md`) holds the rules for how the
  code is built, with examples from the repo and a review checklist.
- **The code style skill** (`.agents\skills\code-style\SKILL.md`) holds the rules for how the code
  reads, with examples from the repo and a review checklist.
- Each change is made in its own git worktree, built and tested.

## Quality and correctness gates

A change is done when it passes the gates that apply to it.
- **Code rules.** The code passes the formatters, and the review checklists of the code practices and
  code style skills.
- **End-to-end tests** (`vr_mirror\e2e\`). They run VR Mirror's desktop app
  (`simoptix_vr_mirror.exe`) against a simulated Pimax headset and game, and against a simulated game
  with the OpenXR layer. They capture the app's window the way OBS does and measure what a
  viewer would see. A change to `vr_mirror\core\` or the desktop app ends with a run of the whole
  suite. A test's limits are never loosened to get a pass without the author's approval.
- **Real recordings.** A problem seen in a real recording first becomes a test that plays the recorded
  session back.
- **The refactor guard.** A change meant to keep behaviour, such as moving code or splitting it into
  stages, must reproduce every output of the build before it, byte for byte
  (`.agents\skills\dev\reference\e2e.md`).

## Measurements

- Decisions rest on numbers: e2e measures, logs, recordings analysed frame by frame, and screenshots
  measured by script. A new method becomes the default only when its measurements are better.
- What was measured, and every approach tried and dropped with the measured reason, is recorded per
  part in `.agents\skills\dev\findings\`.
- The docs say what was not checked, such as which headsets and OBS versions have not been tried.

## Review

- The author reviews the code.
- The code is also reviewed as a whole against the same rules. The author decides which findings are
  acted on.

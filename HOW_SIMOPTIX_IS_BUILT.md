# How SimOptix is built

## The dev workflow

The workflow is written down, and every agent follows it.
- **The dev skill** (`.agents\skills\dev\SKILL.md`) holds the rules for the whole repo and the order
  to work in. The rules cover using only the game's official interfaces, a minimal footprint beside the
  game, and the code's layers and which way they depend. `AGENTS.md` points every coding agent at the
  skills.
- **The code style skill** (`.agents\skills\code-style\SKILL.md`) holds the rules for how the code
  reads, with examples from the repo and a review checklist.
- **The docs skill** (`.agents\skills\docs\SKILL.md`) holds the rules for the docs: who reads each
  level, plain words, and only what exists.
- Each change is made in its own git worktree, built, tested, and committed as one small commit. Its
  subject says what changed, and its body says why.
- Docs change in the same commit as the code they describe.

## Quality and correctness gates

A change is done when it passes the gates that apply to it.
- **Builds.** Every target that includes a changed file is built, with no warnings at `/W3`.
- **Check scripts.** The code style and docs skills each have a script that checks their rules before a
  commit.
- **End-to-end tests** (`vr_mirror\e2e\`). They run VR Mirror's real, unmodified desktop app
  (`simoptix_vr_mirror.exe`) against a simulated Pimax headset and game, or against a simulated game
  with the OpenXR layer's own code in it. They capture its window the way OBS does and measure what a
  viewer would see. A change to `vr_mirror\core\` or the desktop app ends with a run of the whole
  suite. A test's limits are never loosened to get a pass without the author's approval.
- **Real recordings.** A problem seen in a real recording first becomes a test that plays the recorded
  session back. The test fails on the build that has the problem, before the fix.
- **The refactor guard.** A change meant to keep behaviour, such as moving code or splitting it into
  stages, must reproduce every output of the build before it, byte for byte
  (`.agents\skills\dev\reference\e2e.md`, "Every output: `--golden` and the refactor guard").
- **In the headset and in OBS.** What the tests can't reach is checked last, by the author, with what to
  look at named.

## Measurements

- Decisions rest on numbers: e2e measures, logs, recordings analysed frame by frame, and screenshots
  measured by script. A new method becomes the default only when its measurements are better.
- What was measured, and every approach tried and dropped with the measured reason, is recorded per
  part in `.agents\skills\dev\findings\`.
- The docs say what was not checked, such as which headsets and OBS versions have not been tried.

## Review

- The author reviews the code.
- The code and the docs are also reviewed as a whole against the same rules. The author decides which
  findings are acted on.

# How SimOptix is built

## The dev workflow

The workflow is tailored to a wide type of coding agents.
- **The dev skill** (`.agents\skills\dev\SKILL.md`) holds the rules for the whole repo and the order
  to work in. The rules cover using only the game's official interfaces, a minimal footprint beside the
  game, and the code's layers and which way they depend. `AGENTS.md` points every coding agent at the
  skills.
- **The code style skill** (`.agents\skills\code-style\SKILL.md`) holds the rules for how the code
  reads, with examples from the repo and a review checklist.
- **The code practices skill** (`.agents\skills\code-practices\SKILL.md`) holds the rules for how the
  code is built: functions, `const`, one owner for each resource, errors across C APIs, and threads. It
  has a review checklist too.
- Each change is made in its own git worktree, built, tested, and committed as one small commit.

## Quality and correctness gates

A change is done when it passes the gates that apply to it.
- **Builds.** Every target that includes a changed file is built, with no warnings at `/W3`.
- **Formatters.** clang-format sets the layout of the C++ and HLSL, and ruff the layout of the Python.
  Every file passes both checks unchanged.
- **Review checklists.** Every changed line is read against the checklists of the code style and code
  practices skills.
- **Checks on GitHub.** Each pull request is checked on GitHub's runners, and is merged only when every
  check passes. They build every part with no warnings, run both formatters on every file, and compare
  the pipeline's output with the expected output (below). The runners have no graphics card or
  headset, so the tests below that need one run by hand.
- **End-to-end tests** (`vr_mirror\e2e\`). They run VR Mirror's real, unmodified desktop app
  (`simoptix_vr_mirror.exe`) against a simulated Pimax headset and game, or against a simulated game
  with the OpenXR layer's own code in it. They capture its window the way OBS does and measure what a
  viewer would see. A change to `vr_mirror\core\` or the desktop app ends with a run of the whole
  suite, and its numbers are compared with earlier runs (`.agents\skills\dev\reference\e2e_results.md`).
  A test's limits are never loosened to get a pass without the author's approval.
- **Real recordings.** A problem seen in a real recording first becomes a test that plays the recorded
  session back. The test fails on the build that has the problem, before the fix.
- **The pipeline's expected output.** The repo holds a fingerprint of every output of the pipeline,
  in 52 offline runs over a fixed set of inputs and settings. The check on GitHub fails on any change
  to them. A change meant to change the output commits the new fingerprints, and says which runs
  changed and why.
- **The refactor guard.** A change meant to keep behaviour, such as moving code or splitting it into
  stages, must reproduce every output of the build before it, byte for byte
  (`.agents\skills\dev\reference\e2e.md`, "Every output: `--golden` and the refactor guard").
- **The local files test.** The real, built OBS plugins run in a private copy of OBS, and the real
  OpenXR layer in a simulated game. The test checks the files they write under `%LOCALAPPDATA%\simoptix`.
  A change to those files ends with a run, and gets a test case of its own.
- **The OpenXR layer's check program.** Before the layer goes into the game, a program that behaves like
  LMU runs it with the real OpenXR runtime. It fails unless nearly every frame is copied, each copy
  shows its own frame, and the layer's clock matches the runtime's. The signed layer is then tested in
  LMU with Easy Anti-Cheat, outside online sessions first.
- **The Pimax interface check.** The project declares the part of Pimax's interface it uses in its own
  header. Whenever that header changes, a check program compares it with Pimax's SDK.
- **Copies kept in step.** The recording analysis has a Python copy of part of the pipeline, and the
  G-meter's numbers are also in its design's tables. Each copy changes in the same commit as its
  original.
- **Visual output.** Positions and gaps in an overlay are measured in a screenshot by script, and
  sharpness is judged at 1:1. Each render of the G-meter's image layers is checked by script, and a
  layer meant to stay the same must match the committed one pixel for pixel.
- **In the headset and in OBS.** What the tests can't reach is checked last, by the author, with what to
  look at named. After each install, OBS's log is read for the plugins' errors and crashes.

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

---
name: issues
description: Writing SimOptix's GitHub issues and comments. The title, what the first lines say, the measured facts, where to look, when it is done, the labels, and what stays out (personal information, the maintainer's PC, how the work went), with the check before posting. Use whenever an issue, or a comment on an issue or a pull request, is written or edited.
---

# Writing SimOptix's issues and comments

Use this whenever an issue, or a comment on an issue or a pull request, is written or edited. The terms
of this repo are in `.agents\skills\dev\reference\terms.md`. Issues and comments are public, and GitHub
keeps every earlier version of them. What stays out of every text on GitHub, and the check before
posting, are in `.agents\skills\dev\reference\github_text.md`. Read it too. Pull requests and commit
messages have their own skill (`.agents\skills\pull-requests\SKILL.md`).

An issue is read by whoever picks it up, often an agent with no other context. It holds everything
they need to start, and when to stop.

## What an issue holds

- **The title** is `Area: what is wrong or wanted`: `Telemetry: the reader drops LMU every 5 s while
  the game is in its menus`.
- **The first lines** say what is wrong or wanted, as the reader of the root README or the reviewer
  would see it. They also say what isn't known yet, if that is the question.
- **Measured:** the facts, with numbers, units and conditions. Describe the setup only as far as the
  point needs it (`reference\github_text.md`).
- **Not the cause:** what was ruled out, and why, when something was.
- **The cause,** if it is known, and the measurement that shows it.
- **Where to look:** the files, what to log, and the test that would show the problem. A pipeline bug
  names the e2e scenario or replay that should reproduce it (dev skill, "What gets a scenario").
- **Not verified:** what wasn't checked.
- **Related:** the issues and pull requests it builds on, as `#<number>`.
- **Done when:** what is true when the issue can be closed, as checks someone can make.

A wanted change says **Why** and **What it should do** in place of Measured and Not the cause. Leave
out a section that would be empty.

Plans belong in an issue: what to try, and in what order. How the work went doesn't. Nor does the
maintainer's PC, or the agent's notes, memory and other sessions.

## Labels

| Label | When |
|---|---|
| `bug` | Something works wrong |
| `enhancement` | Something new is wanted, or something that works should work better |
| `documentation` | Only the docs or the skills change |

The type labels `added`, `changed`, `fixed`, `removed`, `security` and `internal` are for pull
requests (pull-requests skill, "The label").

## Comments

- A comment adds facts, with numbers: a new measurement, a cause found, a test that now fails.
- It doesn't retell the issue, or tell how the work went.
- When a comment changes what the issue is about, change the issue's text too, so it stays right for
  the next reader.

## Posting

An agent posts an issue or a comment only when the maintainer asked for it.

1. Write the text into a file outside the repo, such as the agent's scratch folder.
2. Check it (`reference\github_text.md`, "Check before posting"):

   ```
   python -I .agents\skills\dev\check_github_text.py --issue-text <file>
   ```

3. Post it from the file:

   ```
   gh issue create --title "<title>" --label <label> --body-file <file>
   ```

   A comment: `gh issue comment <number> --body-file <file>`, or `gh pr comment` for a pull request.
4. Check what was posted: `check_github_text.py --issue <number>`.

To edit an issue, change the file, check it again, and post it with
`gh issue edit <number> --body-file <file>`. The old text stays in GitHub's edit history, so an edit
doesn't take personal information out.

## Examples

| Bad | Why | Good |
|---|---|---|
| `Flicker bug` | No area, and doesn't say what is seen | `OpenXR Mirror: one frame after a game stall shows the game's image half drawn` |
| Seen in `<date> <time>.mp4` at 21:14 | A recording's name and a clock time | One recording, OpenXR Mirror on Smooth, 4K canvas, with the frame trace on |
| It happens sometimes | No numbers | The game frame before it came after a 64 ms stall, the longest in the recording |
| Fix the flicker | Can't be checked | An e2e scenario with a stall followed by two frames at once shows no half-drawn frame |
| I found this while working on the installer in another session | How the work went, and another session | (delete) |

## Checklist before posting

- [ ] The title is `Area: what is wrong or wanted`
- [ ] The first lines say what is wrong or wanted
- [ ] Measured facts have numbers, units and conditions
- [ ] Where to look, what wasn't verified, and Done when as checks someone can make
- [ ] No personal information, nothing about the maintainer's PC, nothing of how the work went
- [ ] `check_github_text.py --issue-text` run, and its lines read
- [ ] Posted from a file, and checked again on GitHub

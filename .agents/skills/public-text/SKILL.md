---
name: public-text
description: What stays out of everything SimOptix makes public - the repo's files (READMEs, all Markdown, findings, code and comments, script help, test data, file names), commit messages, branch names, and GitHub's issues, pull requests, comments and releases. Personal information, the maintainer's PC, the agent's work, and the check before committing or posting. Use whenever anything is written that is committed, pushed or posted.
---

# What stays out of public text

Use this whenever anything is written that is committed, pushed or posted. The terms of this repo are
in `.agents\skills\dev\reference\terms.md`. The other skills say how each kind of text is written:
the docs skill for docs, `code-style` for code and comments, `pull-requests` for commit messages and
pull requests, `issues` for issues and comments. This one says what stays out of all of them.

## What is public

Everything that leaves the PC, and everything in the repo:
- every file in the repo: READMEs and every other Markdown file, the skills and their findings, code
  and its comments, the help text of scripts, test data such as the replays' headers, and the names
  of files and folders
- commit messages, branch names and tags
- on GitHub: issues, pull requests, comments, labels, release notes and the files of a release

A commit can't be changed once it is on `main`, and git keeps every commit. GitHub keeps every earlier
version of an issue, a pull request or a comment, and anyone can open it. So the text is right before
it is committed or posted. An edit doesn't take anything out.

## Personal information stays out

- recording file names, which carry the date and the time (OBS names a recording by its start)
- the dates and clock times of the maintainer's sessions, and how long a named or dated session
  lasted. A count of frames or ticks gives the length away, so it stays only where nothing beside it
  says when or where the session was.
- the date of work done on a session's data: a study of a recording, its frame trace or its OBS log,
  or of eye images read from the game. That work is usually done on the day of the session, so its
  date says when the maintainer drove.
- tracks, cars, and anything else that says when or what the maintainer drove
- local paths, user names, machine names, email addresses, tokens and the values of environment
  variables
- images, because a screenshot or a frame shows the track and the car. Ask the maintainer before
  committing or posting one, and before cropping or blurring it.

Describe the setup only as far as the point needs it: "one recording, OpenXR Mirror on Look-ahead, 4K
canvas". Name a recording or a driving session by what it shows ("the dust replay", "a race with late
frames") or by a letter ("race A"), never by its date or its file name.

These may stay:
- the maintainer's name, in the copyright lines and the sign-off, and where a screenshot shows it
- dates of work that uses no session's data: a commit, a fix, a release, a run of the e2e suite or of
  `stage_replay`
- commit hashes, and numbers measured in the e2e suite
- video times inside a recording that has no name or date (`18:31`), when the point needs them
- paths inside the repo, and folders that are the same on every PC (`%LOCALAPPDATA%\simoptix\`,
  OBS's plugin folder, Program Files)
- the makes of cars where the text is about a product, not a session ("like the in-car G-meters of
  Porsche's Sport Chrono")

The session's details stay in local reports and in the data folder (dev skill, step 5).

## The maintainer's PC and the agent's work stay out

- **The PC:** its folders, their owners and permissions, what is installed, and errors met there.
- **The agent's work:** its notes, its memory, and other sessions.
- **How the work went:** who asked for what, a plan or an earlier request the result differs from,
  what was "found along the way".

An agent tells the maintainer these in chat.

## Check before committing or posting

Run the check, then read the whole text for each kind above. A clean run is not the end, because the
script knows only the commonest tracks and cars, and no one's name.

| What | Command, from the repo's root |
|---|---|
| What is staged, before each commit | `python -I .agents\skills\public-text\check_public_text.py --staged` |
| A commit message, a pull request's description, before posting | `python -I .agents\skills\public-text\check_public_text.py --pr-text <file>` |
| An issue or a comment, before posting | `python -I .agents\skills\public-text\check_public_text.py <file>` |
| A branch's commit messages, before pushing | `python -I .agents\skills\public-text\check_public_text.py --commits main..<branch>` |
| What was posted | `python -I .agents\skills\public-text\check_public_text.py --pr <number>` or `--issue <number>` |
| Every file in the repo | `python -I .agents\skills\public-text\check_public_text.py --repo` |

Write a text for GitHub, or a commit message, into a file outside the repo, such as the agent's
scratch folder, and post it from the file: `gh pr create --body-file <file>`,
`gh issue create --body-file <file>`, `git commit -F <file>`. Windows PowerShell 5.1 splits a quoted
argument that holds double quotes, so `--body "..."` and `-m "..."` break on such text.

| Kind it lists | What it looks for |
|---|---|
| `TOKEN`, `EMAIL` | A GitHub or API token, an email address outside a `Signed-off-by:` line. These fail the run |
| `DATE`, `TIME` | `2026-11-08`, `8 November`, `21:14` |
| `RECORDING` | A video file's name with a number in it, such as OBS's date and time |
| `LENGTH` | Counts of frames, ticks, events or laps, and minutes or hours |
| `TRACK_CAR` | LMU's tracks and the makes of its cars |
| `PATH` | A drive letter outside the folders that are the same on every PC, a user folder |
| `PC`, `THE WORK` | Folders recreated, access denied, owners. Other sessions, notes, memory, "along the way" |
| `PLAN` | "Next step", "follow-up", "later", "once ... is on main". Only with `--pr-text`, `--pr` and `--commits` |

## Examples

| Bad | Why | Good |
|---|---|---|
| Seen in `<date> <time>.mp4` at 21:14, in the Porsche at Spa | A recording's name, a clock time, a car and a track | Seen in one recording, OpenXR Mirror on Look-ahead |
| `- 2026-03-14, a race at Sebring, 52 min` (in a findings file) | A session's date, its track and its length | `- race A, the longest of four sessions` |
| The second start, at 21:05 | A clock time of the maintainer's session | The second start, with the load note |
| `// Measured 2026-03-14 on six eye images read from the layer` (in a code comment) | A study of a session's data, dated on the session's day | `// Measured on six eye images read from the layer` |
| The flicker came 41312 frames into the session | The count gives the session's length | The flicker is one output frame, after a 64 ms stall |
| `# A recording '<date> <time>_s2.mp4' (OBS names files ...)` in a script's help | A real recording's name, in code | `# A recording 'YYYY-MM-DD HH-MM-SS_s2.mp4'` |
| The plugin folders were recreated by a process running as administrator | The state of the maintainer's PC | (delete, and tell the maintainer in chat) |
| I've left a note in the shared notes folder | The agent's notes | (delete) |
| At the maintainer's request this uses the PR description, not fragment files | How the work went | (delete) |

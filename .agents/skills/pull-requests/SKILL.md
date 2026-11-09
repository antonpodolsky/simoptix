---
name: pull-requests
description: Writing SimOptix's pull requests and commit messages. The title, the description from the template, the label, and what stays out (personal information, the maintainer's PC, how the work went, plans), with the check before posting. Use whenever a commit message is written, or a pull request is opened or its title or description edited.
---

# Writing SimOptix's pull requests and commit messages

Use this whenever a commit message is written, or a pull request is opened or edited. The terms of
this repo are in `.agents\skills\dev\reference\terms.md`. Commit messages and pull requests are public,
and they stay. What stays out of every text on GitHub, and the check before posting, are in
`.agents\skills\dev\reference\github_text.md`. Read it too. The dev skill
(`.agents\skills\dev\SKILL.md`) says when to commit (step 6) and how a change reaches `main` (step 7).

A pull request is read by a reviewer who sees only its diff, and later by anyone reading the history.
Write for them.

## Only this change

A pull request's description and its commit messages cover the change and nothing else. They say what
the branch changes against `main`, why, what was verified and what wasn't. These stay out:
- **How the work went:** who asked for what, a plan or an earlier request the change differs from,
  what was "found along the way".
- **The maintainer's PC:** its folders, their owners and permissions, what is installed, errors met
  there. A check that wasn't run is named as not run, with a reason about the change ("not run,
  because nothing in `vr_mirror\core\` changed"), never about the PC.
- **The agent's work:** its notes, its memory, and other sessions.
- **What comes next:** plans, next steps, follow-ups, and work that isn't on `main` or in this branch.
  An agent tells the maintainer in chat. A plan worth keeping becomes an issue
  (`.agents\skills\issues\SKILL.md`), when the maintainer asks for one.
- **Personal information** (`reference\github_text.md`, "Personal information stays out").

## Commit messages

- **The subject** is `Area: what changed`, written for developers: `Overlays: the G-meter's live G
  moved up 1 px`. The area is the part's name (`Pipeline`, `OpenXR layer`, `Docs`, `Dev skill`).
- **The body** says why: the problem, why the change solves it this way, and what was checked, with
  numbers. Wrap it at about 72 columns.
- **The sign-off and the attribution** are in the dev skill, step 6: `git commit -s`, and the
  attribution line the agent's own instructions give, if any.
- **Commit from a file:** `git commit -s -F <file>`. Windows PowerShell 5.1 splits a quoted argument
  that holds double quotes, so `-m "..."` breaks on such text.

## The pull request

- **The title** is `Area: what changed`, like a commit subject. With one commit, it is that commit's
  subject. With several, it says what they do together: `SimOptix files: one place names them and
  keeps the newest 10 of each kind`.
- **The description** follows the template (`.github\pull_request_template.md`):
  - **What changed and why:** the problem in a sentence or two, then a bullet per change, each with a
    bold lead. `Fixes #<number>` names an issue the pull request closes. `Part of #<number>` names one
    it doesn't close.
  - **What was verified:** the builds, the e2e results if `vr_mirror\core\` or
    `vr_mirror\mirror_app\` changed (compared with `reference\e2e_results.md`), and what was checked by
    hand, with numbers. Then **Not verified:** each check that wasn't run or couldn't be, with its
    reason.
  - **Changelog:** the lines users read, written by the docs skill ("The changelog").
  - An agent ends the description with the attribution line its own instructions give, if any.
- **Plain words.** Short sentences with one idea each, and no semicolons. Code names and paths are
  fine where the reviewer needs them.
- **The label.** Every pull request takes exactly one type label, which decides whether it goes into
  `CHANGELOG.md` (`reference\release.md`, "The changelog, from each pull request"):
  - A change users notice gets `added`, `changed`, `fixed`, `removed` or `security`, and one to three
    lines in the description's Changelog section.
  - Any other change gets `internal` and an empty Changelog section: tests, refactors, contributors'
    docs, the skills, the build, findings.

## Opening a pull request

1. Write the description from the template into a file outside the repo, such as the agent's scratch
   folder.
2. Read each sentence as a reviewer who sees only `git diff main...<branch>`. Delete each one that
   reviewer doesn't need ("Only this change").
3. Check the description and the branch's commit messages (`reference\github_text.md`, "Check before
   posting"):

   ```
   python -I .agents\skills\dev\check_github_text.py <file>
   ```

   ```
   python -I .agents\skills\dev\check_github_text.py --commits main..<branch>
   ```

4. Open it from the file:

   ```
   gh pr create --base main --title "<title>" --label <label> --body-file <file>
   ```

5. Check what was posted: `check_github_text.py --pr <number>`.

## Editing a pull request

- Change the file, repeat steps 2 and 3, and post it with `gh pr edit <number> --body-file <file>`.
  Then check what was posted (step 5).
- When the branch changes, the description changes with it: a new commit, `main` merged in, a check
  run again.
- GitHub keeps every earlier version of a description, and anyone can open it. Get it right before
  posting, and don't rely on an edit to take something out.
- Ask the maintainer before editing a pull request that is merged, or that someone else opened.

## Examples

| Bad | Why | Good |
|---|---|---|
| At the maintainer's request this uses the PR description, not fragment files. | How the work went | (delete) |
| **Install fix, found along the way** | How the work went | **Install fix:** `build.bat install` makes the plugin's folders on a PC that has none |
| `build.bat install` failed with "Access is denied", because the plugin folders belong to Administrators now | The state of the maintainer's PC | **`build.bat install` into OBS's plugin folder:** not run. The plugins were checked in the local files test's private copy of OBS instead |
| **Next step:** once the installer is on `main`, it should show the version | A plan, and work on another branch | (delete, and tell the maintainer in chat) |
| I've left a note in the shared notes folder | The agent's notes | (delete) |
| Tested on a recording from last night at Spa | A session's date, a track | Tested on one recording, OpenXR Mirror on Smooth |
| `Fix stuff` | No area, says nothing | `Telemetry: the reader keeps LMU while the game is in its menus` |

## Checklist before posting

- [ ] The title is `Area: what changed`
- [ ] Every sentence is about this change, and a reviewer who sees only the diff needs it
- [ ] No personal information, nothing about the maintainer's PC, nothing of how the work went, no
      plans
- [ ] What was verified has numbers. What wasn't is named, with a reason about the change
- [ ] One type label, and Changelog lines only with a user-facing label
- [ ] `check_github_text.py` run on the description and on the branch's commits, and its lines read
- [ ] Posted from a file, and checked again on GitHub

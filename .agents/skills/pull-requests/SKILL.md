---
name: pull-requests
description: Writing SimOptix's pull requests and commit messages. The title, the description from the template, the label, and what stays out (personal information, the maintainer's PC, how the work went, plans), with the check before posting. Use whenever a commit message is written, or a pull request is opened or its title or description edited.
---

# Writing SimOptix's pull requests and commit messages

Use this whenever a commit message is written, or a pull request is opened or edited. The terms of
this repo are in `.agents\skills\dev\reference\terms.md`. Commit messages and pull requests are public,
and they stay. What stays out of everything public, and the check before posting, are in the
public-text skill (`.agents\skills\public-text\SKILL.md`). Read it too. The dev skill
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
- **Personal information** (public-text skill, "Personal information stays out").

## Commit messages

- **The subject** is `type(scope): what changed`, in the form of the pull request's title ("The
  title", below), written for developers: `fix(overlays): the G-meter's live G moved up 1 px`. Each
  commit takes the type of its own change.
- **The body** says why: the problem, why the change solves it this way, and what was checked, with
  numbers. Wrap it at about 72 columns.
- **The sign-off and the attribution** are in the dev skill, step 6: `git commit -s`, and the
  attribution line the agent's own instructions give, if any.
- **Commit from a file:** `git commit -s -F <file>`. Windows PowerShell 5.1 splits a quoted argument
  that holds double quotes, so `-m "..."` breaks on such text.
- **Check before each commit,** both what is staged and the message, because every line of a commit
  is public: `check_public_text.py --staged` and `check_public_text.py --pr-text <file>`.

## The pull request

- **The title** is `type(scope): what changed`, in the form of Conventional Commits. GitHub makes it
  the subject of the pull request's commit on `main`.
  - **The type** is one of those its label allows ("The label", below): `feat`, `fix`, `perf`,
    `docs`, `refactor`, `test`, `build`, `ci`, `chore` or `style`.
  - **The scope** is the name of the folder that holds the change: a part's (`overlays`, `pipeline`,
    `openxr`, `e2e`, `telemetry`, `setup`) or a skill's (`dev`, `pull-requests`). A change to the
    folders of one name in several parts takes that name (`obs_plugin`). A change across parts, or to
    the files at the repo's root, takes `repo`.
  - **What changed** starts in lower case, unless it starts with a name (`G-meter`, `OBS`).
  - With one commit, the title is that commit's subject. With several, it says what they do
    together: `feat(repo): one place names SimOptix's files and keeps the newest 10 of each kind`.
  - The Changelog check on GitHub fails a title of another form, or with a type its label doesn't
    allow (`release.py --check-pr`).
- **The description** follows the template (`.github\pull_request_template.md`):
  - **What changed and why:** first the issues it closes and works on, one line each ("The issues it
    closes", below). Then the problem in a sentence or two, and a bullet per change, each with a bold
    lead.
  - **What was verified:** the builds, the e2e results if `vr_mirror\core\` or
    `vr_mirror\mirror_app\` changed (compared with `reference\e2e_results.md`), and what was checked by
    hand, with numbers. Then **Not verified:** each check that wasn't run or couldn't be, with its
    reason.
  - **Changelog:** the lines users read, written by the docs skill ("The changelog").
  - The Changelog section ends the description. The release script reads every line after its
    heading as a changelog line, so nothing follows it. A description holds no line that names the
    tool or agent that wrote it ("Generated with ..."), even where the agent's own instructions ask
    for one.
- **Plain words.** Short sentences with one idea each, and no semicolons. Code names and paths are
  fine where the reviewer needs them.
- **The label.** Every pull request takes exactly one type label. Issues take the same labels
  (issues skill, "Labels"). The label decides whether the pull request goes into `CHANGELOG.md`, and
  under which heading (`reference\release.md`, "The changelog, from each pull request"). It also
  decides the types its title may start with:

  | Label | The pull request | Title types | Changelog |
  |---|---|---|---|
  | `feature` | Adds something users notice: a source, a setting, a game, a headset | `feat` | Added |
  | `change` | Changes what users see, set or install, or how well it works | `feat`, `perf`, `docs` | Changed |
  | `bug` | Fixes something users noticed | `fix` | Fixed |
  | `removal` | Removes something users had | `feat`, `refactor` | Removed |
  | `security` | Fixes a security problem | `fix` | Security |
  | `documentation` | Changes only contributors' docs, the skills or findings | `docs` | None |
  | `maintenance` | Changes nothing users notice: tests, refactors, the build, the checks, a release | `refactor`, `test`, `build`, `ci`, `chore`, `perf`, `style` | None |

  - A change users notice gets one to three lines in the description's Changelog section.
  - A pull request labelled `documentation` or `maintenance` has an empty Changelog section.

## The issues it closes

GitHub closes an issue by itself when a pull request that names it with a closing keyword is merged
into `main`. A pull request that names its issues this way leaves no issue open after its work is
done.

- **Find them first.** List the open issues (`gh issue list --state open`). Read the "Done when" of
  each one the change touches, and check each of its items against the branch.
- **`Fixes #<number>`** for each issue whose "Done when" is fully met once the pull request is
  merged. One line per issue, each with its own keyword: `Fixes #12` and `Fixes #27` on two lines.
  In `Fixes #12, #27`, GitHub closes only #12.
- **`Part of #<number>`** for each issue the change works on and leaves open, with what of it this
  change does: `Part of #34: every part builds on each pull request`. GitHub links it without
  closing it.
- **These lines start the description,** under "What changed and why", before the problem.
- **A closing keyword closes the issue wherever it stands.** GitHub reads `fix`, `fixes`, `fixed`,
  `close`, `closes`, `closed`, `resolve`, `resolves` and `resolved` followed by an issue number, in
  any case, anywhere in the description. A sentence about an issue that stays open never puts one of
  these words right before its number: "the fault in #12" instead of "fixes #12 partly".
- **Commit messages name no issue with a closing keyword.** The pull request's description does. A
  keyword in a commit message also closes the issue once the commit reaches `main`, but GitHub then
  doesn't list the pull request as linked to the issue.
- **Check it after posting.** `gh pr view <number> --json closingIssuesReferences` lists the issues
  GitHub will close. It must list exactly the `Fixes` lines.

## Opening a pull request

1. Find the issues the change closes and works on ("The issues it closes"). Write the description
   from the template into a file outside the repo, such as the agent's scratch folder.
2. Read each sentence as a reviewer who sees only `git diff main...<branch>`. Delete each one that
   reviewer doesn't need ("Only this change").
3. Check the description and the branch's commit messages (public-text skill, "Check before committing
   or posting"):

   ```
   python -I .agents\skills\public-text\check_public_text.py --pr-text <file>
   ```

   ```
   python -I .agents\skills\public-text\check_public_text.py --commits main..<branch>
   ```

4. Open it from the file:

   ```
   gh pr create --base main --title "<title>" --label <label> --body-file <file>
   ```

5. Check what was posted: `check_public_text.py --pr <number>`, and the issues GitHub will close:
   `gh pr view <number> --json closingIssuesReferences`.
6. An agent in Claude Code's desktop app turns on Auto-fix for the pull request. The app then wakes
   the agent when a check fails, the branch conflicts with `main`, or a review comment arrives. The
   agent fixes the branch, checks the fix and pushes it. A review comment's request waits for the
   maintainer.

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
| Tested on a recording from last night at Spa | A session's date, a track | Tested on one recording, OpenXR Mirror on Look-ahead |
| `Fix stuff` | No type, no scope, says nothing | `fix(telemetry): the reader keeps LMU while the game is in its menus` |
| `Telemetry: the reader keeps LMU while the game is in its menus` | No type | `fix(telemetry): the reader keeps LMU while the game is in its menus` |
| `feat(telemetry): the reader keeps LMU while the game is in its menus`, labelled `bug` | The label `bug` takes the type `fix` | `fix(telemetry): ...` |

## Checklist before posting

- [ ] The title is `type(scope): what changed`, with a type its label allows, and each commit
      subject has the same form
- [ ] The open issues were checked. Each one the change closes has its own `Fixes #<number>` line,
      and each it works on a `Part of #<number>` line. `closingIssuesReferences` lists exactly the
      `Fixes` lines once it is posted
- [ ] Every sentence is about this change, and a reviewer who sees only the diff needs it
- [ ] No personal information, nothing about the maintainer's PC, nothing of how the work went, no
      plans
- [ ] What was verified has numbers. What wasn't is named, with a reason about the change
- [ ] One type label, and Changelog lines only with a label that has a Changelog heading
- [ ] The Changelog section ends the description, and no line names the tool or agent that wrote it.
      `python -I .agents\skills\dev\release.py --check-pr <number>` passes once it is posted
- [ ] `check_public_text.py` run on what each commit staged, on the description and on the branch's
      commits, and its lines read
- [ ] Posted from a file, and checked again on GitHub

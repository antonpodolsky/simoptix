# Text posted on GitHub

Terms: `reference\terms.md`. Issues, pull requests, comments, commit messages and release notes are
public. A commit can't be changed once it is on `main`. An edited issue, pull request or comment keeps
its old text in GitHub's edit history, which anyone can open. So the text is right before it is
posted. This file says what stays out of all of them, and how to check. The issues skill
(`.agents\skills\issues\SKILL.md`) and the pull-requests skill (`.agents\skills\pull-requests\SKILL.md`)
say how each is written.

## Personal information stays out

These stay out of every text on GitHub:
- recording file names, which carry the date and the time
- the dates and clock times of the maintainer's sessions, and how long they lasted. Counts of frames,
  ticks or events give the length away.
- tracks, cars, and anything else that says when or what the maintainer drove
- local paths, user names, machine names, email addresses, tokens and the values of environment
  variables
- images, because a screenshot or a frame shows the track and the car. Ask the maintainer before
  posting one, and before cropping or blurring it.

Describe the setup only as far as the point needs it: "one recording, OpenXR Mirror on Smooth, 4K
canvas".

These may stay:
- the maintainer's name, also where a screenshot shows it
- dates of code: a commit, "the plugin before this change"
- commit hashes, and numbers measured in the e2e suite
- paths inside the repo, and folders that are the same on every PC (`%LOCALAPPDATA%\simoptix\`, OBS's
  plugin folder)

The session's details stay in local reports and in the data folder (`SKILL.md`, step 5).

## The maintainer's PC and the agent's work stay out

- **The PC:** its folders, their owners and permissions, what is installed, and errors met there.
- **The agent's work:** its notes, its memory, and other sessions.
- **How the work went:** who asked for what, a plan or an earlier request the result differs from,
  what was "found along the way".

An agent tells the maintainer these in chat.

## Check before posting

1. Write the text to a file outside the repo, such as the agent's scratch folder.
2. Run the check on it:

   ```
   python -I .agents\skills\dev\check_github_text.py <file>
   ```

   For an issue, add `--issue-text`, because plans belong in an issue. For the commits of a branch,
   run it with `--commits main..<branch>` before pushing.
3. Read the lines it lists, and read the whole text for each kind above. A clean run is not the end,
   because the script knows only the commonest tracks and cars, and no one's name.
4. Post from the file: `gh issue create --body-file <file>`, `gh pr create --body-file <file>`,
   `git commit -F <file>`. Windows PowerShell 5.1 splits a quoted argument that holds double quotes,
   so `--body "..."` and `-m "..."` break on such text.
5. Run the check again on what was posted: `--issue <number>` or `--pr <number>`.

| Kind it lists | What it looks for |
|---|---|
| `TOKEN`, `EMAIL` | A GitHub or API token, an email address outside a `Signed-off-by:` line. These fail the run |
| `DATE`, `TIME` | `2026-11-08`, `8 November`, `21:14` |
| `RECORDING` | A video file's name (`.mp4`, `.mkv` and the like) |
| `LENGTH` | Counts of frames, ticks, events or laps, and minutes or hours |
| `TRACK_CAR` | LMU's tracks and the makes of its cars |
| `PATH` | A drive letter, a user folder |
| `PC`, `THE WORK` | Administrator rights, permissions, owners. Other sessions, notes, memory, "along the way" |
| `PLAN` | "Next step", "follow-up", "later", "once ... is on main". Not listed for an issue |

## Examples

| Bad | Why | Good |
|---|---|---|
| Seen in `<date> <time>.mp4` at 21:14, in the Porsche at Spa | A recording's name, a clock time, a car and a track | Seen in one recording, OpenXR Mirror on Smooth |
| The flicker came 41312 frames into the session | The count gives the session's length | The flicker is one output frame, after a 64 ms stall |
| The plugin folders in `C:\ProgramData\obs-studio\plugins\` were recreated by a process running as administrator | The state of the maintainer's PC | (delete, and tell the maintainer in chat) |
| I've left a note in the shared notes folder | The agent's notes | (delete) |
| At the maintainer's request this uses the PR description, not fragment files | How the work went | (delete) |

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Writes the next release's section of CHANGELOG.md from the merged pull requests, and prints it as the
GitHub Release's notes. reference\release.md, beside this script, has the whole release.

The release is the version in VERSION. Its changes are the pull requests merged into the commits since
the newest release tag (v<version>) before HEAD, on the first-parent line, so a patch branch gets only
its own. Each pull request carries exactly one type label:
  added, changed, fixed, removed, security   a change users notice. The lines of its description's
                                              "Changelog" section go under that heading.
  internal                                    a change only contributors notice. It is left out.
The script refuses, and changes nothing, when a pull request has no type label or more than one, when
a user-facing one has no Changelog lines or more than three, when an internal one has some, when no
pull request has a change for users, or when CHANGELOG.md already has a section for this version.
Commits that came from no pull request are listed, and left out.

Usage, from anywhere inside the repo:
  python -I .agents\skills\dev\release.py [--dry-run] [--since <tag>] [--date YYYY-MM-DD]
                                          [--prs <file>] [--notes-file <file>]
  python -I .agents\skills\dev\release.py --notes [--notes-file <file>]

  --dry-run      print the section without changing CHANGELOG.md
  --since        the release to start after (default: the newest release tag before HEAD, or the
                 first commit when there is none)
  --date         the release's date (default: today)
  --prs          read the pull requests from a file that `gh pr list --state merged --json
                 number,title,body,labels,mergeCommit,url` wrote, instead of asking GitHub
  --notes        print the notes of the version in VERSION from CHANGELOG.md again, changing nothing
  --notes-file   also write the notes to this file, in UTF-8 (for `gh release create --notes-file`)

Asking GitHub needs the GitHub CLI (gh), signed in.
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CHANGELOG = os.path.join(ROOT, "CHANGELOG.md")
VERSION_FILE = os.path.join(ROOT, "VERSION")

# The headings of a release's section, in this order, by the label that puts a pull request there.
TYPES = {
    "added": "Added",
    "changed": "Changed",
    "fixed": "Fixed",
    "removed": "Removed",
    "security": "Security",
}
INTERNAL = "internal"
MAX_ENTRIES = 3

# gh lists at most this many pull requests. More than that since one release means the list was cut.
PR_LIMIT = 1000

VERSION_FORM = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
SECTION_HEADING = re.compile(r"^## \[(\d+\.\d+\.\d+)\]")
LINK_LINE = re.compile(r"^\[\d+\.\d+\.\d+\]: ")
HEADING = re.compile(r"^#{1,6}\s")
CHANGELOG_HEADING = re.compile(r"^#{1,6}\s*changelog\s*$", re.I)
HTML_COMMENT = re.compile(r"<!--.*?-->", re.S)


class ReleaseError(Exception):
    """A reason to stop before anything is changed."""


def run(args):
    """The output of a command run in the repo, or ReleaseError with what it said."""
    try:
        result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    except FileNotFoundError:
        raise ReleaseError(f"{args[0]} was not found. Install it, or check that it is on PATH.")

    if result.returncode != 0:
        raise ReleaseError(f"{' '.join(args)} failed: {result.stderr.strip()}")

    return result.stdout


def read_version():
    with open(VERSION_FILE, encoding="utf-8") as f:
        version = f.read().strip()

    if not VERSION_FORM.match(version):
        raise ReleaseError(f"VERSION must hold one version, three numbers like 0.1.0, not {version!r}.")

    return version


def version_key(version):
    return tuple(int(part) for part in version.split("."))


def read_changelog():
    with open(CHANGELOG, encoding="utf-8") as f:
        return f.read().replace("\r\n", "\n")


def repo_url():
    """The repo's GitHub address, from the remote origin (git@github.com:o/r.git or https://...)."""
    remote = run(["git", "remote", "get-url", "origin"]).strip()
    match = re.match(r"^(?:git@github\.com:|https://github\.com/)(.+?)(?:\.git)?/?$", remote)

    if not match:
        raise ReleaseError(f"The remote origin isn't on GitHub: {remote}")

    return "https://github.com/" + match.group(1)


def newest_release_tag():
    """The newest v<version> tag before HEAD, or None before the first release."""
    result = subprocess.run(
        ["git", "describe", "--tags", "--match", "v[0-9]*", "--abbrev=0", "HEAD"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        return None

    return result.stdout.strip()


def first_parent_commits(since):
    """(sha, subject) of each commit after `since` up to HEAD on the first-parent line, oldest first."""
    span = f"{since}..HEAD" if since else "HEAD"
    out = run(["git", "log", "--first-parent", "--reverse", "--format=%H %s", span])
    return [tuple(line.split(" ", 1)) for line in out.splitlines() if line]


def merged_pull_requests(prs_file, since):
    fields = "number,title,body,labels,mergeCommit,url"

    if prs_file:
        with open(prs_file, encoding="utf-8") as f:
            return json.load(f)

    args = ["gh", "pr", "list", "--state", "merged", "--limit", str(PR_LIMIT), "--json", fields]

    if since:
        # A day early, so the time zone of the tag's date can't leave a pull request out.
        tagged = datetime.date.fromisoformat(run(["git", "log", "-1", "--format=%cs", since]).strip())
        args += ["--search", f"merged:>={tagged - datetime.timedelta(days=1)}"]

    prs = json.loads(run(args))

    if len(prs) >= PR_LIMIT:
        raise ReleaseError(f"gh listed {PR_LIMIT} pull requests, its limit here, so some may be missing.")

    return prs


def changelog_lines(body):
    """The lines of the description's Changelog section, comments removed, or None without one."""
    lines = HTML_COMMENT.sub("", (body or "").replace("\r\n", "\n")).split("\n")
    start = next((i for i, line in enumerate(lines) if CHANGELOG_HEADING.match(line.strip())), None)

    if start is None:
        return None

    section = []

    for line in lines[start + 1 :]:
        if HEADING.match(line):
            break

        section.append(line.rstrip())

    return section


def entries_of(lines):
    """One string per bullet ("- " or "* "). Indented lines continue the bullet above them.
    ([entries], problem): the problem names a line that is neither."""
    entries = []

    for line in lines:
        if not line.strip():
            continue

        if line.startswith(("- ", "* ")):
            entries.append(line[2:].strip())
        elif line.startswith(" ") and entries:
            entries[-1] += " " + line.strip()
        else:
            return entries, f'a line that isn\'t a bullet starting with "- ": {line.strip()!r}'

    return entries, None


def sort_pull_requests(commits, prs):
    """(the pull request of each commit, oldest first; the commits that came from none)."""
    by_commit = {(pr.get("mergeCommit") or {}).get("oid"): pr for pr in prs}
    ordered, direct = [], []

    for sha, subject in commits:
        if sha in by_commit:
            ordered.append(by_commit[sha])
        else:
            direct.append(f"{sha[:7]} {subject}")

    return ordered, direct


def collect_entries(prs):
    """({type: [entries]}, [problems]) for the pull requests, in their order."""
    entries = {label: [] for label in TYPES}
    problems = []

    for pr in prs:
        name = f"#{pr['number']} {pr['title']}"
        labels = [
            label["name"]
            for label in pr.get("labels", [])
            if label["name"] in TYPES or label["name"] == INTERNAL
        ]
        lines = changelog_lines(pr.get("body"))
        found, bad_line = entries_of(lines or [])

        if len(labels) != 1:
            problems.append(f"{name}: needs exactly one of the labels {', '.join(list(TYPES) + [INTERNAL])}")
        elif labels[0] == INTERNAL:
            if found or bad_line:
                problems.append(f"{name}: is labelled internal, but its Changelog section has text")
        elif bad_line:
            problems.append(f"{name}: its Changelog section has {bad_line}")
        elif not found:
            problems.append(f"{name}: is labelled {labels[0]}, but its description has no Changelog lines")
        elif len(found) > MAX_ENTRIES:
            problems.append(f"{name}: has {len(found)} Changelog lines, at most {MAX_ENTRIES}")
        else:
            entries[labels[0]].extend(found)

    return entries, problems


def notes_text(entries):
    """The groups of a release's section: what GitHub's Release shows under its title."""
    parts = []

    for label, heading in TYPES.items():
        if entries[label]:
            parts.append(f"### {heading}\n\n" + "\n".join(f"- {entry}" for entry in entries[label]))

    return "\n\n".join(parts) + "\n"


def with_section(changelog, version, date, notes, url):
    """The changelog with the new section above the newest one, and its link with the others."""
    lines = changelog.rstrip("\n").split("\n")
    links = [i for i, line in enumerate(lines) if LINK_LINE.match(line)]
    link = f"[{version}]: {url}/releases/tag/v{version}"

    if links:
        lines.insert(links[0], link)
    else:
        lines += ["", link]

    first = next((i for i, line in enumerate(lines) if SECTION_HEADING.match(line)), None)

    if first is None:
        first = links[0] if links else len(lines) - 2

        while first > 0 and not lines[first - 1].strip():
            first -= 1

        first += 1

    section = f"## [{version}] - {date}\n\n{notes}".rstrip("\n").split("\n")
    lines[first:first] = section + [""]
    return "\n".join(lines).rstrip("\n") + "\n"


def section_notes(changelog, version):
    """The notes of a version's section in the changelog, or ReleaseError without one."""
    lines = changelog.split("\n")
    start = next((i for i, line in enumerate(lines) if line.startswith(f"## [{version}]")), None)

    if start is None:
        raise ReleaseError(f"CHANGELOG.md has no section for {version}.")

    body = []

    for line in lines[start + 1 :]:
        if SECTION_HEADING.match(line) or LINK_LINE.match(line):
            break

        body.append(line)

    return "\n".join(body).strip("\n") + "\n"


def write_notes(notes, path):
    if path:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(notes)


def make_release(args):
    version = read_version()
    changelog = read_changelog()

    if any(line.startswith(f"## [{version}]") for line in changelog.split("\n")):
        raise ReleaseError(f"CHANGELOG.md already has a section for {version}. Bump VERSION first.")

    since = args.since or newest_release_tag()

    if since and since.startswith("v") and VERSION_FORM.match(since[1:]):
        if version_key(version) <= version_key(since[1:]):
            raise ReleaseError(f"VERSION says {version}, which isn't after {since}. Bump VERSION first.")

    commits = first_parent_commits(since)
    prs, direct = sort_pull_requests(commits, merged_pull_requests(args.prs, since))
    entries, problems = collect_entries(prs)

    if problems:
        raise ReleaseError(
            "Fix these pull requests on GitHub, then run this again:\n  " + "\n  ".join(problems)
        )

    if not any(entries.values()):
        raise ReleaseError(f"No pull request since {since or 'the first commit'} has a change for users.")

    for line in direct:
        print(f"not from a pull request, so left out: {line}", file=sys.stderr)

    notes = notes_text(entries)

    if not args.dry_run:
        updated = with_section(changelog, version, args.date, notes, repo_url())

        with open(CHANGELOG, "w", encoding="utf-8", newline="\n") as f:
            f.write(updated)

    write_notes(notes, args.notes_file)
    print(f"## [{version}] - {args.date}\n\n{notes}", end="")


def print_notes(args):
    notes = section_notes(read_changelog(), read_version())
    write_notes(notes, args.notes_file)
    print(notes, end="")


def main():
    parser = argparse.ArgumentParser(description="Writes the next release's section of CHANGELOG.md.")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--since")
    parser.add_argument("--date", default=datetime.date.today().isoformat())
    parser.add_argument("--prs")
    parser.add_argument("--notes", action="store_true")
    parser.add_argument("--notes-file")
    args = parser.parse_args()

    # The notes go to a console that may not be UTF-8 (Windows' code page), and they hold × and –.
    sys.stdout.reconfigure(encoding="utf-8")

    try:
        if args.notes:
            print_notes(args)
        else:
            make_release(args)
    except ReleaseError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

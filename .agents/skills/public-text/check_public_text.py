# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Lists the lines of a public text that may break .agents\skills\public-text\SKILL.md.

It reads what is staged for a commit, the repo's files, a text for GitHub in a file, a posted pull
request or issue, or a branch's commit messages. It prints each line that looks like personal
information, a detail of the maintainer's PC, how the work went, or (in pull requests and commit
messages) a plan. It is a review list: a clean run is not the end, and a listed line may be fine, such
as the date of a commit. The exit code is 1 only for a token or an email address, which never belong
in public text.

Usage, from anywhere inside the repo:
  python -I .agents\skills\public-text\check_public_text.py --staged
  python -I .agents\skills\public-text\check_public_text.py --repo
  python -I .agents\skills\public-text\check_public_text.py <file> [<file> ...] [--pr-text]
  python -I .agents\skills\public-text\check_public_text.py --pr <number>
  python -I .agents\skills\public-text\check_public_text.py --issue <number>
  python -I .agents\skills\public-text\check_public_text.py --commits <range>      (such as main..HEAD)

--pr-text reads the files as a pull request's description or a commit message, where plans don't
belong.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# The skills that show bad text as examples, and this script's own patterns.
EXAMPLE_FILES = {
    ".agents/skills/public-text/SKILL.md",
    ".agents/skills/public-text/check_public_text.py",
    ".agents/skills/pull-requests/SKILL.md",
    ".agents/skills/issues/SKILL.md",
}

MONTHS = (
    r"January|February|March|April|May|June|July|August|September|October|November|December|"
    r"Jan|Feb|Mar|Apr|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec"
)

# LMU's tracks and the makes of its cars, the ones a session's text is most likely to name.
# "Le Mans" is a track, and "Le Mans Ultimate" the game.
TRACKS_AND_CARS = (
    r"Le Mans(?! Ultimate)|Sarthe|Spa|Francorchamps|Monza|Sebring|Fuji|Bahrain|Imola|Portim[aã]o|"
    r"Interlagos|Lusail|Qatar|COTA|Circuit of the Americas|Paul Ricard|Silverstone|Barcelona|"
    r"Porsche|Ferrari|Toyota|Peugeot|Cadillac|BMW|Alpine|Lamborghini|Aston Martin|Corvette|Ford|"
    r"McLaren|Lexus|Isotta|Glickenhaus|Vanwall|Oreca|Ligier|Ginetta|Mercedes|Genesis|Duqueine"
)

# Each kind: (name, pattern, fails the run). The names are what the output prints.
PERSONAL_KINDS = [
    ("TOKEN", re.compile(r"\b(ghp_|gho_|github_pat_|sk-ant-|sk-)[A-Za-z0-9_-]{8,}"), True),
    # git@github.com is the address of a remote, not a person's.
    ("EMAIL", re.compile(r"(?<![\w.+-])(?!git@)[\w.+-]+@[\w-]+\.[\w.]+"), True),
    ("DATE", re.compile(rf"\b20\d\d-\d\d-\d\d\b|\b\d{{1,2}} ({MONTHS})\b|\b({MONTHS}) \d{{1,2}}\b"), False),
    ("TIME", re.compile(r"\b([01]?\d|2[0-3]):[0-5]\d(:[0-5]\d)?\b"), False),
    # A name with a number in it, as OBS gives each recording its start; "<recording>.mp4" stays.
    ("RECORDING", re.compile(r"[\w\-. ]*\d[\w\-. ]*\.(mp4|mkv|mov|flv|avi|webm)\b", re.IGNORECASE), False),
    (
        "LENGTH",
        re.compile(
            r"\b\d[\d,]{2,}\s*(frames|ticks|events|laps)\b|\b\d+(\.\d+)?\s*(min|minutes|hours|h)\b",
            re.IGNORECASE,
        ),
        False,
    ),
    ("TRACK_CAR", re.compile(rf"\b({TRACKS_AND_CARS})\b"), False),
    # Program Files, ProgramData and Windows are the same on every PC, and so is a folder named by an
    # environment variable (%LOCALAPPDATA%).
    (
        "PATH",
        re.compile(
            r"\b[A-Za-z]:[\\/]+(?!Program Files|ProgramData|Windows|[\\/])|[\\/]Users[\\/]|~[\\/]",
            re.IGNORECASE,
        ),
        False,
    ),
]

PC_AND_WORK_KINDS = [
    (
        "PC",
        re.compile(
            r"\b(access is denied|owned by|owner of|recreated by|running as administrator|elevated process|"
            r"on your PC|on my PC)\b",
            re.IGNORECASE,
        ),
        False,
    ),
    (
        "THE WORK",
        re.compile(
            r"\b(along the way|at the maintainer's request|as you (asked|chose|said)|you asked|"
            r"another session's|other sessions|shared notes|my notes|my memory|memory file|"
            r"I found|we found|originally|the original plan|as planned|instead of the plan)\b",
            re.IGNORECASE,
        ),
        False,
    ),
]

PLAN_KIND = (
    "PLAN",
    re.compile(
        r"\b(next step|follow-up|follow up|(?<!and )later|for now|once .+ is on main|should then|TODO)\b",
        re.IGNORECASE,
    ),
    False,
)

# Lines that carry what git and the sign-off need, and may name an email address.
SIGN_OFF = re.compile(r"^\s*(Signed-off-by|Co-authored-by):", re.IGNORECASE)

HUNK_START = re.compile(r"^@@ -\d+(,\d+)? \+(\d+)")


def run(command):
    result = subprocess.run(command, check=False, cwd=ROOT, capture_output=True, text=True, encoding="utf-8")

    if result.returncode != 0:
        sys.exit(f"{' '.join(command)} failed: {result.stderr.strip()}")

    return result.stdout


def read_text(path):
    """Returns the file's text, or None for a file that isn't UTF-8 text (an image, a DLL)."""
    try:
        with open(path, encoding="utf-8") as file:
            return file.read()
    except UnicodeDecodeError:
        return None


def numbered(text):
    return list(enumerate(text.split("\n"), 1))


def staged_lines():
    """Returns (name, [(line number, line)]) for each staged file: its name, and the lines it adds."""
    diff = run(["git", "diff", "--cached", "-U0", "--no-color", "--no-renames"])
    files, path, n = [], None, 0

    for line in diff.split("\n"):
        if line.startswith("+++ "):
            path = None if line == "+++ /dev/null" else line[6:]

            if path is not None and path not in EXAMPLE_FILES:
                files.append((path, [(0, path)]))

            continue

        hunk = HUNK_START.match(line)

        if hunk:
            n = int(hunk.group(2))
            continue

        if line.startswith("+") and files and files[-1][0] == path:
            files[-1][1].append((n, line[1:]))
            n += 1

    return files


def repo_lines():
    """Returns (name, [(line number, line)]) for each tracked text file outside third_party."""
    files = []

    for path in run(["git", "ls-files"]).split("\n"):
        if not path or path.startswith("third_party/") or path in EXAMPLE_FILES:
            continue

        text = read_text(os.path.join(ROOT, path))

        if text is not None:
            files.append((path, [(0, path)] + numbered(text)))

    return files


def texts_to_check(args):
    """Returns (name, [(line number, line)]) pairs from the chosen source."""
    if args.staged:
        return staged_lines()

    if args.repo:
        return repo_lines()

    if args.pr is not None:
        body = run(["gh", "pr", "view", str(args.pr), "--json", "body", "--jq", ".body"])
        return [(f"pull request {args.pr}", numbered(body))]

    if args.issue is not None:
        body = run(["gh", "issue", "view", str(args.issue), "--json", "body", "--jq", ".body"])
        return [(f"issue {args.issue}", numbered(body))]

    if args.commits is not None:
        log = run(["git", "log", "--format=%h %B%x00", args.commits])
        messages = [message.strip("\n") for message in log.split("\0") if message.strip()]
        return [(f"commit {message.split()[0]}", numbered(message)) for message in messages]

    return [(path, numbered(read_text(path) or "")) for path in args.files]


def kinds_for(args):
    kinds = PERSONAL_KINDS + PC_AND_WORK_KINDS

    if args.pr_text or args.pr is not None or args.commits is not None:
        kinds.append(PLAN_KIND)

    return kinds


def findings(name, lines, kinds):
    """Returns (printed line, fails the run) for each line that a kind matches. Line 0 is the name."""
    found = []

    for n, line in lines:
        if SIGN_OFF.match(line):
            continue

        for kind, pattern, fails in kinds:
            if pattern.search(line):
                where = f"{name} (file name)" if n == 0 else f"{name}:{n}"
                found.append((f"{kind:10} {where}: {line.strip()[:110]}", fails))

    return found


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("files", nargs="*")
    parser.add_argument("--staged", action="store_true")
    parser.add_argument("--repo", action="store_true")
    parser.add_argument("--pr", type=int)
    parser.add_argument("--issue", type=int)
    parser.add_argument("--commits")
    parser.add_argument("--pr-text", action="store_true")
    args = parser.parse_args()

    sources = [
        bool(args.files),
        args.staged,
        args.repo,
        args.pr is not None,
        args.issue is not None,
        args.commits is not None,
    ]

    if sum(sources) != 1:
        parser.error("give files, or one of --staged, --repo, --pr, --issue or --commits")

    kinds = kinds_for(args)
    found = []

    for name, lines in texts_to_check(args):
        found += findings(name, lines, kinds)

    for line, _ in found:
        print(line)

    failed = sum(1 for _, fails in found if fails)
    print(f"{failed} problems, {len(found) - failed} lines to review")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

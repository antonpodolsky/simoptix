# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Lists the lines of a text for GitHub that may break .agents\skills\dev\reference\github_text.md.

It reads an issue, a pull request description, a comment or commit messages, before or after posting,
and prints each line that looks like personal information, a detail of the maintainer's PC, how the
work went, or (outside issues) a plan. It is a review list: a clean run is not the end, and a listed
line may be fine, such as the date of a commit. The exit code is 1 only for a token or an email
address, which never belong in the text.

Usage, from anywhere inside the repo:
  python -I .agents\skills\dev\check_github_text.py <file> [<file> ...] [--issue-text]
  python -I .agents\skills\dev\check_github_text.py --pr <number>
  python -I .agents\skills\dev\check_github_text.py --issue <number>
  python -I .agents\skills\dev\check_github_text.py --commits <range>      (such as main..HEAD)

--issue-text reads the files as an issue, whose plans and next steps belong in it.
"""

import argparse
import re
import subprocess
import sys

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
    ("EMAIL", re.compile(r"[\w.+-]+@[\w-]+\.[\w.]+"), True),
    ("DATE", re.compile(rf"\b20\d\d-\d\d-\d\d\b|\b\d{{1,2}} ({MONTHS})\b|\b({MONTHS}) \d{{1,2}}\b"), False),
    ("TIME", re.compile(r"\b([01]?\d|2[0-3]):[0-5]\d(:[0-5]\d)?\b"), False),
    ("RECORDING", re.compile(r"[^\s`'\"]*\.(mp4|mkv|mov|flv|avi|webm)\b", re.IGNORECASE), False),
    (
        "LENGTH",
        re.compile(
            r"\b\d[\d,]{2,}\s*(frames|ticks|events|laps)\b|\b\d+\s*(min|minutes|hours|h)\b", re.IGNORECASE
        ),
        False,
    ),
    ("TRACK_CAR", re.compile(rf"\b({TRACKS_AND_CARS})\b"), False),
    # A folder named by an environment variable (%LOCALAPPDATA%) is the same on every PC, so it stays.
    ("PATH", re.compile(r"\b[A-Za-z]:[\\/]|[\\/]Users[\\/]|~[\\/]", re.IGNORECASE), False),
]

PC_AND_WORK_KINDS = [
    (
        "PC",
        re.compile(
            r"\b(administrator|elevated|access is denied|permission|owner of|owned by|on your PC|on my PC)\b",
            re.IGNORECASE,
        ),
        False,
    ),
    (
        "THE WORK",
        re.compile(
            r"\b(along the way|at the maintainer's request|as you (asked|chose|said)|you asked|"
            r"this session|other session|another session|shared notes|my notes|my memory|memory file|"
            r"I found|we found|"
            r"originally|the original plan|as planned|instead of the plan)\b",
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


def run(command):
    result = subprocess.run(command, check=False, capture_output=True, text=True, encoding="utf-8")

    if result.returncode != 0:
        sys.exit(f"{' '.join(command)} failed: {result.stderr.strip()}")

    return result.stdout


def texts_to_check(args):
    """Returns (name, text) pairs from the files, or from GitHub or git."""
    if args.pr is not None:
        return [
            (
                f"pull request {args.pr}",
                run(["gh", "pr", "view", str(args.pr), "--json", "body", "--jq", ".body"]),
            )
        ]

    if args.issue is not None:
        body = run(["gh", "issue", "view", str(args.issue), "--json", "body", "--jq", ".body"])
        return [(f"issue {args.issue}", body)]

    if args.commits is not None:
        log = run(["git", "log", "--format=%h %B%x00", args.commits])
        return [(f"commit {message.split()[0]}", message) for message in log.split("\0") if message.strip()]

    texts = []
    for path in args.files:
        with open(path, encoding="utf-8") as file:
            texts.append((path, file.read()))

    return texts


def kinds_for(args):
    is_issue = args.issue is not None or args.issue_text
    kinds = PERSONAL_KINDS + PC_AND_WORK_KINDS

    if not is_issue:
        kinds.append(PLAN_KIND)

    return kinds


def findings(name, text, kinds):
    """Returns (printed line, fails the run) for each line of the text that a kind matches."""
    found = []

    for n, line in enumerate(text.split("\n"), 1):
        if SIGN_OFF.match(line):
            continue

        for kind, pattern, fails in kinds:
            if pattern.search(line):
                found.append((f"{kind:10} {name}:{n}: {line.strip()[:110]}", fails))

    return found


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("files", nargs="*")
    parser.add_argument("--pr", type=int)
    parser.add_argument("--issue", type=int)
    parser.add_argument("--commits")
    parser.add_argument("--issue-text", action="store_true")
    args = parser.parse_args()

    sources = [bool(args.files), args.pr is not None, args.issue is not None, args.commits is not None]

    if sum(sources) != 1:
        parser.error("give files, or one of --pr, --issue or --commits")

    kinds = kinds_for(args)
    found = []

    for name, text in texts_to_check(args):
        found += findings(name, text, kinds)

    for line, _ in found:
        print(line)

    failed = sum(1 for _, fails in found if fails)
    print(f"{failed} problems, {len(found) - failed} lines to review")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

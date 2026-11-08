# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Checks SimOptix's Markdown docs against the rules in .agents\skills\docs\SKILL.md.

Hard checks (the exit code is 1 if one fails):
  - no semicolons outside code blocks and inline code
  - every relative link and every repo path in backticks resolves
  - every term in the "Terms coined in this repo" table of .agents\skills\dev\reference\terms.md
    has a row in a terms table at or before its first use (dev skill files may link to terms.md in
    their first lines instead)
Review list (printed, never fails): lines with negative statements or plan words, and outside terms
(the "Outside terms" table of terms.md) used without a row in that document's terms table.

Usage, from anywhere inside the repo:
  python -I .agents\skills\docs\check_docs.py [--words] [--no-review]
"""

import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SKILL_DIR = os.path.join(ROOT, ".agents", "skills", "dev")
TERMS_FILE = os.path.join(SKILL_DIR, "reference", "terms.md")
EXEMPT_NAMES = {"LICENSE", "THIRD_PARTY_NOTICES.md"}

# Files that talk about terms and rules themselves, or that are legal text.
NO_TERM_CHECK = {".agents/skills/docs/SKILL.md"}

NEGATIVE = re.compile(
    r"\b(doesn't|does not|don't|do not|never|no longer|isn't|aren't|cannot|can't|"
    r"neither|nor|not yet|none of|nothing|without|for now|yet)\b",
    re.I,
)


def DocFiles():
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT, capture_output=True, text=True).stdout
    files = []
    for rel in out.split():
        if rel.startswith("third_party/") or os.path.basename(rel) in EXEMPT_NAMES:
            continue

        files.append(rel)

    return files


def Lines(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read().split("\n")


def ProseLines(lines):
    """(number, text) of every line outside fenced code blocks, inline code removed."""
    fenced = False
    for n, line in enumerate(lines, 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue

        if fenced:
            continue

        yield n, re.sub(r"`[^`]*`", "``", line)


def CheckSemicolons(path, lines):
    return [(n, t) for n, t in ProseLines(lines) if ";" in t]


def PathCandidates(lines):
    fenced = False
    for n, line in enumerate(lines, 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue

        if fenced:
            continue

        for m in re.finditer(r"\]\(([^)]+)\)", line):
            target = m.group(1).split("#")[0]
            if target and not re.match(r"^(https?:|mailto:)", target):
                yield n, target, True

        for m in re.finditer(r"`([^`]+)`", line):
            yield n, m.group(1), False


# Paths that are real but not in the repo: the response files' relative paths, folders under
# %LOCALAPPDATA%, the data folder's files, a branch name, and removed code named in findings.
IGNORE_PATHS = {
    "..\\..\\shared\\log.cpp",
    ".claude\\worktrees\\",
    "traces\\",
    "mod/launch.ps1",
    "traction\\lmu_tyres_1.csv",
    "plugin_config\\simoptix_overlays",
    "tools\\lmu\\grip_replay",
    "lookahead_sim\\",
    "claude/openxr-source-cf46ca",
    ".h/.cpp",
    "vr_mirror\\core\\pipeline\\pipeline_builder",
}

SKIP_PATH = re.compile(r"^(%|[A-Za-z]:|HK|\.\.\.|<|--|/[A-Za-z]|~)|[*<>%=+,]|\.\.\.|\b(bin|obj|out)[\\/]|\s")
GENERATED = re.compile(r"(simoptix_vr_mirror\.ini$|\.exe$|^Support[\\/])")


_ALL = None


def AllFiles():
    global _ALL
    if _ALL is None:
        out = subprocess.run(
            ["git", "ls-files", "-co", "--exclude-standard"], cwd=ROOT, capture_output=True, text=True
        ).stdout

        _ALL = ["/" + f for f in out.split("\n") if f]

    return _ALL


def Resolves(doc, target):
    """A path resolves when it exists from the repo root, the doc's folder or the dev skill's folder,
    or when it is the tail of a tracked path (a folder table lists `pimax\\`, a file table `a.h/.cpp`)."""

    target = target.replace("\\", "/")
    target = re.sub(r":\d+$", "", target)
    target = re.sub(r"(\.\w+)/\.\w+$", r"\1", target)
    bases = [ROOT, os.path.join(ROOT, os.path.dirname(doc)), SKILL_DIR]
    if any(os.path.exists(os.path.join(b, target)) for b in bases):
        return True

    tail = "/" + target.strip("/")
    return any(f.endswith(tail) or (tail + "/") in f for f in AllFiles())


def CheckPaths(doc, lines):
    bad = []
    for n, target, is_link in PathCandidates(lines):
        if not is_link:
            if target in IGNORE_PATHS:
                continue

            if not re.search(r"[\\/]", target) or SKIP_PATH.search(target) or GENERATED.search(target):
                continue

            if not re.match(r"^[A-Za-z0-9_.\-\\/]+$", target):
                continue

        if not Resolves(doc, target):
            bad.append((n, target))

    return bad


def Terms(section):
    """The terms in the table under the heading `## <section>` of terms.md."""
    terms = []
    in_section = False
    for line in Lines(os.path.relpath(TERMS_FILE, ROOT)):
        if line.startswith("## "):
            in_section = line[3:].strip().lower() == section.lower()
            continue

        if not in_section:
            continue

        if not line.startswith("|") or line.startswith("|---") or line.startswith("| Term"):
            continue

        cell = line.split("|")[1].strip().replace("`", "").replace("**", "")
        for alt in cell.split(" / "):
            if alt:
                terms.append(alt.strip())

    return terms


def InTermsTable(lines, n):
    """True when line n is a row of a table whose header starts with | Term |."""
    if not lines[n - 1].startswith("|"):
        return False

    i = n - 1
    while i > 0 and lines[i - 1].startswith("|"):
        i -= 1

    return lines[i].lower().startswith("| term |")


def CheckTerms(doc, lines, terms):
    if doc in NO_TERM_CHECK or os.path.abspath(os.path.join(ROOT, doc)) == TERMS_FILE:
        return []

    if doc.startswith(
        (".agents/skills/dev/", ".agents/skills/code-style/", ".agents/skills/code-practices/")
    ) and any("terms.md" in l for l in lines[:15]):
        return []

    table_rows = []
    for n, line in enumerate(lines, 1):
        if line.startswith("|") and not line.startswith("|---"):
            table_rows.append((n, line.split("|")[1].lower() if line.count("|") > 1 else ""))

    prose = [(n, t) for n, t in ProseLines(lines) if not InTermsTable(lines, n)]

    # The title and the opening paragraph may use a term when they explain it themselves.
    opening_end, seen_text = 0, False
    for n, text in prose:
        if n == 1:
            continue

        if text.strip():
            seen_text = True
        elif seen_text:
            opening_end = n
            break

    bad = []
    for term in terms:
        rx = re.compile(r"\b" + re.escape(term.lower()) + r"s?\b")
        first = None
        for n, text in prose:
            if n <= opening_end:
                continue

            if rx.search(text.lower()):
                first = n
                break

        if first is None:
            continue

        defined = any(rx.search(cell) and n <= first for n, cell in table_rows)
        if not defined:
            bad.append((first, term))

    return bad


def main():
    args = sys.argv[1:]
    terms = Terms("Terms coined in this repo")
    outside = Terms("Outside terms")
    failed = 0
    review = []
    words = []
    for doc in DocFiles():
        lines = Lines(doc)
        words.append((doc, len(" ".join(lines).split())))
        for n, t in CheckSemicolons(doc, lines):
            print(f"SEMICOLON  {doc}:{n}: {t.strip()[:100]}")
            failed += 1

        for n, target in CheckPaths(doc, lines):
            print(f"PATH       {doc}:{n}: {target}")
            failed += 1

        for n, term in CheckTerms(doc, lines, terms):
            print(f'TERM       {doc}:{n}: "{term}" is used before a table row defines it')
            failed += 1

        for n, term in CheckTerms(doc, lines, outside):
            review.append(
                f'OUTSIDE    {doc}:{n}: "{term}" has no row in a terms table. Does this reader know it?'
            )

        for n, t in ProseLines(lines):
            if NEGATIVE.search(t):
                review.append(f"NEGATIVE   {doc}:{n}: {t.strip()[:110]}")

    if "--no-review" not in args:
        print("\n".join(review))

    if "--words" in args:
        for doc, count in words:
            print(f"{count:7d}  {doc}")

        print(f"{sum(c for _, c in words):7d}  total")

    print(
        f"\n{failed} problems, {len(review)} lines to review, {len(words)} files, "
        f"{len(terms)} coined and {len(outside)} outside terms"
    )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

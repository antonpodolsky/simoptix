# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compares pipeline_replay's golden file (--golden) run by run, with another golden file or with the
expected output in golden_expected.txt.

A golden file holds every output of the pipeline over a fixed set of runs. A run starts at its
`# <input> <settings>` line. golden_expected.txt, beside this script, holds each run's SHA-256 and
label, so that a change in any run's output is caught without keeping the 26 MB file in the repo.

golden_expected.txt is the output of pipeline_replay as GitHub's runners build it. Another compiler
rounds some values in the last bits differently, so a golden file built elsewhere can differ from it
with nothing changed. A change that is meant takes the golden_expected.txt that the failed Pipeline
output run uploads, and commits it with the change.

Usage:
  python -I vr_mirror\\e2e\\pipeline_replay\\golden_compare.py <before> <after> [--markdown FILE]
  python -I vr_mirror\\e2e\\pipeline_replay\\golden_compare.py --expected <golden> [--markdown FILE]
  python -I vr_mirror\\e2e\\pipeline_replay\\golden_compare.py --write-expected <golden>

The first compares two golden files, as the refactor guard of .agents\\skills\\dev\\reference\\e2e.md
does. The second compares a golden file with golden_expected.txt, as the Pipeline output check on
GitHub does. The third writes golden_expected.txt from a golden file, as that check does for its
upload. --markdown also appends the result to FILE as Markdown (GitHub's job summary). The exit code
is 0 when the outputs are the same, 1 when they differ, and 2 when a file can't be read.
"""

import argparse
import hashlib
import os
import sys

EXPECTED = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden_expected.txt")


def read_runs(path):
    """Returns {run label: its lines} and the labels in the file's order."""
    runs = {}
    order = []
    label = None

    with open(path, encoding="utf-8") as file:
        for line in file:
            if line.startswith("# "):
                label = line[2:].rstrip("\n")
                order.append(label)
                runs[label] = []
            elif label is not None:
                runs[label].append(line)

    return runs, order


def fingerprint(lines):
    return hashlib.sha256("".join(lines).encode("utf-8")).hexdigest()


def read_expected(path):
    """Returns {run label: its SHA-256} and the labels in the file's order."""
    expected = {}
    order = []

    with open(path, encoding="utf-8") as file:
        for line in file:
            if line.startswith("#") or not line.strip():
                continue

            digest, label = line.rstrip("\n").split(" ", 1)
            expected[label] = digest
            order.append(label)

    return expected, order


def write_expected(golden_path):
    runs, order = read_runs(golden_path)

    with open(EXPECTED, "w", encoding="utf-8", newline="\n") as file:
        file.write("# The SHA-256 of each run of `pipeline_replay --golden`, and its label. Written by\n")
        file.write("# golden_compare.py --write-expected. The Pipeline output check compares with it.\n")

        for label in order:
            file.write(f"{fingerprint(runs[label])} {label}\n")

    print(f"{len(order)} runs written to {EXPECTED}")


def first_difference(before, after):
    """The number of the first line of a run (1 = the line after its label) that differs."""
    for number, (old, new) in enumerate(zip(before, after), start=1):
        if old != new:
            return number

    return min(len(before), len(after)) + 1


def report(changed, added, removed, total, what):
    """The report's lines, and whether nothing changed. `changed` holds (label, detail) pairs."""
    if not changed and not added and not removed:
        return [f"pipeline_replay --golden: identical to {what}, all {total} runs."], True

    lines = [
        f"pipeline_replay --golden: {len(changed)} of {total} runs changed against {what}, "
        f"{len(added)} new, {len(removed)} gone."
    ]
    lines += [f"- changed: `{label}`{detail}" for label, detail in changed]
    lines += [f"- new: `{label}`" for label in added]
    lines += [f"- gone: `{label}`" for label in removed]
    return lines, False


def compare_files(before_path, after_path):
    before, before_order = read_runs(before_path)
    after, after_order = read_runs(after_path)

    changed = [
        (label, f", from its line {first_difference(before[label], after[label])}")
        for label in after_order
        if label in before and before[label] != after[label]
    ]
    added = [label for label in after_order if label not in before]
    removed = [label for label in before_order if label not in after]
    return report(changed, added, removed, len(after_order), "the other file")


def compare_expected(golden_path):
    expected, expected_order = read_expected(EXPECTED)
    runs, order = read_runs(golden_path)

    changed = [
        (label, "") for label in order if label in expected and expected[label] != fingerprint(runs[label])
    ]
    added = [label for label in order if label not in expected]
    removed = [label for label in expected_order if label not in runs]
    lines, same = report(changed, added, removed, len(order), "golden_expected.txt")

    if not same:
        lines.append(
            "If the change is meant, commit the golden_expected.txt that the Pipeline output check "
            "uploads with the change. golden_expected.txt comes from GitHub's build: a golden file "
            "built with another compiler can differ in the last bits with nothing changed."
        )

    return lines, same


def main():
    parser = argparse.ArgumentParser(description="Compares pipeline_replay golden files, run by run.")
    parser.add_argument("files", nargs="*")
    parser.add_argument(
        "--expected", action="store_true", help="compare one golden file with golden_expected.txt"
    )
    parser.add_argument(
        "--write-expected", action="store_true", help="write golden_expected.txt from one golden file"
    )
    parser.add_argument("--markdown", help="also append the result to this file")
    args = parser.parse_args()

    wanted = 1 if args.expected or args.write_expected else 2

    if len(args.files) != wanted:
        parser.error(f"give {wanted} golden file(s)")

    try:
        if args.write_expected:
            write_expected(args.files[0])
            return 0

        if args.expected:
            lines, same = compare_expected(args.files[0])
        else:
            lines, same = compare_files(args.files[0], args.files[1])
    except OSError as error:
        print(f"cannot read a file: {error}")
        return 2

    print("\n".join(lines))

    if args.markdown:
        with open(args.markdown, "a", encoding="utf-8") as summary:
            summary.write("\n".join(lines) + "\n")

    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())

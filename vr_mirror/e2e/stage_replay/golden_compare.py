# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compares two golden dumps of stage_replay (--golden), run by run.

The refactor guard of .agents\\skills\\dev\\reference\\e2e.md: a change that must not change behaviour
reproduces the build before it byte for byte. This says which runs changed, which are new and which
are gone, where `fc /b` says only that the files differ. A run starts at its `# <input> <settings>`
line. The checks on GitHub run it on each pull request against the build before it.

Usage:
  python -I vr_mirror\\e2e\\stage_replay\\golden_compare.py <before> <after> [--markdown FILE]

--markdown also appends the result to FILE as Markdown (GitHub's job summary). The exit code is 0
when the dumps are identical, 1 when they differ, and 2 when a file can't be read.
"""

import argparse
import sys


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


def first_difference(before, after):
    """The number of the first line of a run (1 = the line after its label) that differs."""
    for number, (old, new) in enumerate(zip(before, after), start=1):
        if old != new:
            return number

    return min(len(before), len(after)) + 1


def compare(before_path, after_path):
    """Returns the report's lines, and whether the dumps are identical."""
    before, before_order = read_runs(before_path)
    after, after_order = read_runs(after_path)

    changed = [label for label in after_order if label in before and before[label] != after[label]]
    added = [label for label in after_order if label not in before]
    removed = [label for label in before_order if label not in after]
    total = len(after_order)

    if not changed and not added and not removed:
        return [f"stage_replay --golden: identical, all {total} runs."], True

    lines = [
        f"stage_replay --golden: {len(changed)} of {total} runs changed, "
        f"{len(added)} new, {len(removed)} gone."
    ]

    for label in changed:
        line = first_difference(before[label], after[label])
        lines.append(f"- changed: `{label}`, from its line {line}")

    lines += [f"- new: `{label}`" for label in added]
    lines += [f"- gone: `{label}`" for label in removed]
    return lines, False


def main():
    parser = argparse.ArgumentParser(description="Compares two stage_replay golden dumps, run by run.")
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--markdown", help="also append the result to this file")
    args = parser.parse_args()

    try:
        lines, identical = compare(args.before, args.after)
    except OSError as error:
        print(f"cannot read a dump: {error}")
        return 2

    print("\n".join(lines))

    if args.markdown:
        with open(args.markdown, "a", encoding="utf-8") as summary:
            summary.write("\n".join(lines) + "\n")

    return 0 if identical else 1


if __name__ == "__main__":
    sys.exit(main())

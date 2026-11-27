# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""Blocks a shell command that breaks one of the dev skill's rules for the shell (step 2, "Edit").

Claude Code runs it before each call of its Bash and PowerShell tools (.claude\\settings.json) and passes
the call as JSON on stdin. Exit code 2 blocks the call, and stderr tells the agent which rule it breaks
and what to do instead. Exit code 0 lets the call run. Any other exit code is an error of the hook
itself, which Claude Code shows before it runs the call.

The rules, and the commands each blocks:
- heredoc: a bash heredoc or here-string that holds a backslash. Bash turns a doubled backslash into
  one, and Python then reads a backslash and the letter after it as an escape.
- python -c: Python code on the command line that holds a backslash, for the same reason, and because
  Windows PowerShell 5.1 splits a quoted argument that holds double quotes.
- PowerShell writes: Set-Content and Out-File, which in Windows PowerShell 5.1 add a byte-order mark and
  garble non-ASCII text.

Check a command by hand, from the repo's root:
    python -I .agents\\skills\\dev\\hooks\\check_shell_command.py --command "<command>"
"""

import argparse
import json
import re
import sys
from dataclasses import dataclass

BLOCK = 2

FILE_INSTEAD = (
    "Write the script or the text to a file in the scratch folder with the file-editing tool, and run "
    "or pass that file: python -I <file>, git commit -s -F <file>, gh pr create --body-file <file>."
)


@dataclass(frozen=True)
class Rule:
    name: str
    pattern: re.Pattern
    needs_backslash: bool
    reason: str


RULES = (
    Rule(
        name="heredoc",
        # <<EOF, <<-EOF, << 'EOF', <<"EOF" and the here-string <<<.
        pattern=re.compile(r"<<<|<<-?\s*['\"]?[A-Za-z_]"),
        needs_backslash=True,
        reason=(
            "This command has a heredoc that holds a backslash. Bash turns a doubled backslash into one, "
            "and Python or the shell then reads a backslash and the letter after it as an escape: the "
            "\\a in \\auto_sync becomes a control character. " + FILE_INSTEAD
        ),
    ),
    Rule(
        name="python -c",
        # python, python3 or py, with or without a folder and .exe, and its options up to -c.
        pattern=re.compile(r"(?i)(?:^|[\s;&|(\\/])(?:python3?|py)(?:\.exe)?(?:\s+-[\w.]+)*?\s+-c\b"),
        needs_backslash=True,
        reason=(
            "This command passes Python code that holds a backslash with python -c. The shell and Python "
            "both read backslashes as escapes, and Windows PowerShell 5.1 splits a quoted argument that "
            "holds double quotes. " + FILE_INSTEAD
        ),
    ),
    Rule(
        name="PowerShell writes",
        # The cmdlet at the start of a statement or a pipeline stage, not a mention of it in a string.
        pattern=re.compile(r"(?im)(?:^|[|;{(]|&&)\s*(?:Set-Content|Out-File)\b"),
        needs_backslash=False,
        reason=(
            "This command writes a file with Set-Content or Out-File. Windows PowerShell 5.1 adds a "
            "byte-order mark and garbles non-ASCII text (an en dash comes out as three other characters). "
            "Write the file with the file-editing tool."
        ),
    ),
)


def broken_rules(command):
    broken = []

    for rule in RULES:
        if rule.needs_backslash and "\\" not in command:
            continue

        if rule.pattern.search(command):
            broken.append(rule)

    return broken


def read_command(args):
    if args.command is not None:
        return args.command

    call = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    return call.get("tool_input", {}).get("command", "")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--command", help="check this command instead of the tool call on stdin")
    args = parser.parse_args()

    try:
        command = read_command(args)
    except (json.JSONDecodeError, UnicodeDecodeError, AttributeError) as error:
        print(f"check_shell_command: could not read the tool call: {error}", file=sys.stderr)
        return 1

    broken = broken_rules(command)

    if not broken:
        return 0

    reasons = [
        f'Blocked by the dev skill\'s rules for the shell (step 2, "Edit"): {rule.reason}' for rule in broken
    ]
    print("\n\n".join(reasons), file=sys.stderr)
    return BLOCK


if __name__ == "__main__":
    sys.exit(main())

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""Blocks a shell command that breaks one of the dev skill's rules for the shell (step 2, "Edit").

Claude Code runs it before each call of its Bash and PowerShell tools (.claude\\settings.json) and passes
the call as JSON on stdin. Exit code 2 blocks the call, and stderr tells the agent which rule it breaks
and what to do instead. Exit code 0 lets the call run. Any other exit code is an error of the hook
itself, which Claude Code shows before it runs the call.

The rules, and the commands each blocks:
- unquoted heredoc: a heredoc or here-string whose end marker isn't quoted, and that holds a backslash.
  Bash turns two backslashes into one there.
- python -c: Python code on the command line that holds a backslash. Bash changes backslashes in double
  quotes, and Windows PowerShell 5.1 splits a quoted argument that holds double quotes.
- Windows path in Python: Python code in a quoted heredoc with a Windows path in backslashes, such as
  a drive letter or two folders. Python reads a backslash in a string as an escape. A path with one
  backslash looks like an escape such as a newline, so the hook can't tell it from one.
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

BACKSLASH = re.compile(r"\\")
# <<EOF, <<-EOF, << EOF, and a here-string that isn't in single quotes (<<<"text").
UNQUOTED_HEREDOC = re.compile(r"<<-?[ \t]*[A-Za-z_]|<<<[ \t]*(?!')")
# <<'EOF', <<"EOF", <<\EOF, and a here-string in single quotes (<<<'text').
QUOTED_HEREDOC = re.compile(r"<<-?[ \t]*(?:'|\"|\\)[A-Za-z_]|<<<[ \t]*'")
# python, python3 or py, with or without a folder and .exe.
PYTHON = re.compile(r"(?i)(?:^|[\s;&|(\\/])(?:python3?|py)(?:\.exe)?\b")
# The same, with its options up to -c, and a backslash after it.
PYTHON_C_BACKSLASH = re.compile(r"(?is)(?:^|[\s;&|(\\/])(?:python3?|py)(?:\.exe)?(?:\s+-[\w.]+)*?\s+-c\b.*\\")
# A drive letter or two folders (shared\build\x), each with one backslash. Python's own doubled
# backslashes (shared\\build) don't match.
WINDOWS_PATH = re.compile(r"(?<![\w\\])[A-Za-z]:\\(?!\\)|(?<![\w.\\-])[\w.-]+\\[\w.-]+\\[\w.-]+")
# The cmdlet at the start of a statement or a pipeline stage, not a mention of it in a string.
POWERSHELL_WRITE = re.compile(r"(?im)(?:^|[|;{(]|&&)\s*(?:Set-Content|Out-File)\b")

FORWARD_SLASHES = "Write paths with forward slashes (vr_mirror/obs_plugin), which Windows and Python accept."


@dataclass(frozen=True)
class Rule:
    name: str
    # The command breaks the rule when every pattern matches.
    patterns: tuple
    reason: str


RULES = (
    Rule(
        name="unquoted heredoc",
        patterns=(UNQUOTED_HEREDOC, BACKSLASH),
        reason=(
            "This command has a heredoc with an unquoted end marker, and it holds a backslash. Bash turns "
            "two backslashes into one there, and Python then reads a backslash in front of a name as an "
            "escape: in front of auto_sync it makes a control character. Quote the end marker (<<'EOF'), "
            "so bash passes the text as written. " + FORWARD_SLASHES
        ),
    ),
    Rule(
        name="python -c",
        patterns=(PYTHON_C_BACKSLASH,),
        reason=(
            "This command passes Python code that holds a backslash with python -c. Bash changes "
            "backslashes in double quotes, and Windows PowerShell 5.1 splits a quoted argument that holds "
            "double quotes. Pass the code in a heredoc with a quoted end marker (python - <<'EOF'). "
            + FORWARD_SLASHES
        ),
    ),
    Rule(
        name="Windows path in Python",
        patterns=(PYTHON, QUOTED_HEREDOC, WINDOWS_PATH),
        reason=(
            "This command runs Python code with a Windows path in backslashes. Python reads a backslash in "
            "a string as an escape: in front of auto_sync it makes a control character. " + FORWARD_SLASHES
        ),
    ),
    Rule(
        name="PowerShell writes",
        patterns=(POWERSHELL_WRITE,),
        reason=(
            "This command writes a file with Set-Content or Out-File. Windows PowerShell 5.1 adds a "
            "byte-order mark and garbles non-ASCII text (an en dash comes out as three other characters). "
            "Write the file with the file-editing tool."
        ),
    ),
)


def broken_rules(command):
    return [rule for rule in RULES if all(pattern.search(command) for pattern in rule.patterns)]


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

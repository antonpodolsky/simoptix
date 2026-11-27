# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""Runs each hook in this folder on its cases, the way Claude Code runs it, and checks what it decides.

Each case is a tool call that the hook must block (exit code 2) or let run (exit code 0). The hook gets
the call as JSON on stdin, as from Claude Code. A new rule in a hook brings cases that it blocks, and
cases close to them that it lets run.

Run it from the repo's root:
    python -I .agents\\skills\\dev\\hooks\\test_hooks.py
"""

import json
import os
import subprocess
import sys

HOOKS_DIR = os.path.dirname(os.path.abspath(__file__))

BLOCKED = 2
ALLOWED = 0

# (hook, tool, command, expected exit code)
CASES = (
    # unquoted heredoc
    ("check_shell_command.py", "Bash", "python - <<EOF\nprint('vr_mirror\\\\auto_sync')\nEOF", BLOCKED),
    ("check_shell_command.py", "Bash", "cat <<-EOF > notes.txt\n\tC:\\Program Files\nEOF", BLOCKED),
    ("check_shell_command.py", "Bash", "git commit -s -F - <<EOF\nfix(dev): shared\\build\nEOF", BLOCKED),
    ("check_shell_command.py", "Bash", "python - <<<\"print('a\\\\b')\"", BLOCKED),
    ("check_shell_command.py", "Bash", "cat <<EOF > notes.txt\nplain text\nEOF", ALLOWED),
    ("check_shell_command.py", "Bash", "echo $((1<<3)) shared\\build", ALLOWED),
    # quoted heredoc
    (
        "check_shell_command.py",
        "Bash",
        "python - <<'EOF'\nopen('vr_mirror/obs_plugin/auto_sync.cpp')\nEOF",
        ALLOWED,
    ),
    ("check_shell_command.py", "Bash", "python - <<'EOF'\nprint(\"done\\n\")\nEOF", ALLOWED),
    (
        "check_shell_command.py",
        "Bash",
        "python - <<'EOF'\nprint('shared\\\\build\\\\version.bat')\nEOF",
        ALLOWED,
    ),
    (
        "check_shell_command.py",
        "Bash",
        "git commit -s -F - <<'EOF'\nfix(dev): shared\\build\\version.bat\nEOF",
        ALLOWED,
    ),
    ("check_shell_command.py", "Bash", 'cat << "EOF"\na\\b\nEOF', ALLOWED),
    # Windows path in Python
    (
        "check_shell_command.py",
        "Bash",
        "python - <<'EOF'\nopen('vr_mirror\\obs_plugin\\auto_sync.cpp')\nEOF",
        BLOCKED,
    ),
    (
        "check_shell_command.py",
        "Bash",
        'python3 - <<"EOF"\nimport os\nos.listdir("C:\\Program Files")\nEOF',
        BLOCKED,
    ),
    ("check_shell_command.py", "Bash", "py - <<\\EOF\nopen('shared\\build\\x.txt')\nEOF", BLOCKED),
    ("check_shell_command.py", "Bash", "python - <<<'open(\"shared\\build\\x.txt\")'", BLOCKED),
    # python -c
    ("check_shell_command.py", "Bash", "python -c \"print('a\\\\b')\"", BLOCKED),
    ("check_shell_command.py", "Bash", "python -I -c \"open('shared\\\\log.cpp')\"", BLOCKED),
    ("check_shell_command.py", "PowerShell", "py -3 -c \"print('shared\\x')\"", BLOCKED),
    ("check_shell_command.py", "PowerShell", 'tools\\python\\python.exe -c "print(1)"', ALLOWED),
    ("check_shell_command.py", "Bash", "python3 -c 'print(1)'", ALLOWED),
    ("check_shell_command.py", "PowerShell", "python -I .agents\\skills\\docs\\check_docs.py", ALLOWED),
    ("check_shell_command.py", "Bash", "ruff check -c shared\\x", ALLOWED),
    # PowerShell writes
    ("check_shell_command.py", "PowerShell", "Set-Content -Path notes.txt -Value 'x'", BLOCKED),
    ("check_shell_command.py", "PowerShell", "Get-Process | Out-File procs.txt", BLOCKED),
    ("check_shell_command.py", "PowerShell", "$x = 1; set-content notes.txt $x", BLOCKED),
    ("check_shell_command.py", "PowerShell", "if ($a) { Out-File -FilePath a.txt }", BLOCKED),
    ("check_shell_command.py", "PowerShell", 'git grep -n "Set-Content"', ALLOWED),
    ("check_shell_command.py", "Bash", "grep -n 'Out-File' .agents/skills/dev/SKILL.md", ALLOWED),
    ("check_shell_command.py", "PowerShell", "Get-Content notes.txt", ALLOWED),
)


def run_hook(hook, tool, command):
    call = {"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": {"command": command}}
    result = subprocess.run(
        [sys.executable, "-I", os.path.join(HOOKS_DIR, hook)],
        input=json.dumps(call).encode("utf-8"),
        capture_output=True,
        check=False,
    )
    return result.returncode, result.stderr.decode("utf-8", errors="replace")


def main():
    failures = 0

    for hook, tool, command, expected in CASES:
        code, stderr = run_hook(hook, tool, command)

        if code == expected:
            continue

        failures += 1
        print(f"FAIL {hook}, {tool}: expected exit code {expected}, got {code}")
        print(f"  command: {command!r}")

        if stderr:
            print(f"  stderr: {stderr.strip()}")

    print(f"{len(CASES) - failures} of {len(CASES)} cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

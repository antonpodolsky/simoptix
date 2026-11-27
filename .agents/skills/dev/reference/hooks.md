# Claude Code's hooks

Terms: `reference\terms.md`. A skill tells an agent a rule, and the agent can still break it out of
habit. Claude Code runs a hook before each tool call, and the hook can block the call. So a rule that
a script can check on the call itself is also a hook. The agent sees why the call was blocked, and
makes it again the way the rule asks. This file has the hooks, how they run, and how to add a rule to
them.

## The hooks

| Hook | Runs before | Blocks |
|---|---|---|
| `hooks\check_shell_command.py` | Each call of the Bash and PowerShell tools | A bash heredoc or `python -c` whose text holds a backslash, and `Set-Content` or `Out-File` (step 2, "Edit") |

## How they run

- `.claude\settings.json` lists each hook under `PreToolUse`, with the tools it runs for as its
  `matcher`. The hook's path starts at `CLAUDE_PROJECT_DIR`, the folder where the session started. A
  session started in the main folder runs the main folder's copy of the hook, also while it works in a
  worktree.
- The hook gets the tool call as JSON on stdin: `tool_name`, and `tool_input` with the command.
- Exit code 2 blocks the call, and Claude Code hands the hook's stderr to the agent. Exit code 0 lets
  the call run. Any other exit code is an error of the hook. Claude Code shows it, and runs the call.
- A hook runs before every call of its tools, so it reads only the command and returns at once.
- Other agents don't run these hooks. For them the rules are in the skills alone.

## Adding a rule

A rule belongs in a hook when it is always wrong and can be told from the command alone, such as a
command that writes a file with `Set-Content`. A rule that needs judgment stays in the skills only.

1. Write the rule in the skills first. The hook enforces a rule the skills state, and its message
   names where.
2. Add it to the hook's `RULES`: a pattern that matches the command, and the reason the agent reads.
   The reason says what goes wrong and what to do instead, in one or two sentences.
3. Match the command as it is run, not a mention of it. `git grep "Set-Content"` names the cmdlet and
   runs `git`, so the pattern for `Set-Content` matches it only at the start of a statement or a
   pipeline stage.
4. Add cases to `hooks\test_hooks.py`: commands the rule blocks, and commands close to them that it
   lets run. Run it from the repo's root:

   ```
   python -I .agents\skills\dev\hooks\test_hooks.py
   ```

5. Check one command by hand:

   ```
   python -I .agents\skills\dev\hooks\check_shell_command.py --command "<command>"
   ```

6. A hook for another tool or another moment is a script of its own in `hooks\`, with its own entry
   in `.claude\settings.json` and its cases in `test_hooks.py`. Add its row to the table above.

The Hooks check on GitHub runs `test_hooks.py` on each pull request (`SKILL.md`, step 7).

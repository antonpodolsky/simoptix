---
name: code-style
description: How SimOptix's code looks (C++, HLSL, Python, batch and PowerShell). The formatters that set the layout (clang-format, ruff), and the rules they can't apply - paragraphs, names and comments - with examples from the repo. Use whenever code is written, changed or reviewed, together with the code-practices skill.
---

# How SimOptix's code looks

Use this whenever code is written, changed or reviewed. The `code-practices` skill
(`.agents\skills\code-practices\SKILL.md`) says how code is built. The dev skill
(`.agents\skills\dev\SKILL.md`) has the rules for the whole repo and the names of parts, files and
concepts ("1b. Names"). The terms of this repo are in `.agents\skills\dev\reference\terms.md`.

The goal is code that reads like a document. A reader sees the shape first: paragraphs, names that say
what a thing is, and comments only where something isn't obvious.

## The formatters

Two standard formatters set the layout. Run them on every file a change touches, before committing. Don't
lay out by hand what they set. The dev skill, step 3, says how to install them.

| Language | Formatter | Settings | Format | Check |
|---|---|---|---|---|
| C++, HLSL | clang-format | `.clang-format` | `clang-format -i <files>` | `clang-format --dry-run --Werror <files>` |
| Python | ruff | `ruff.toml` | `ruff format <files>` | `ruff format --check` |

They run on the project's own files and never on `third_party\`. In PowerShell, every C++ and HLSL file
is `git ls-files -- '*.cpp' '*.h' '*.hlsli' ':!third_party'`.

The formatters set:
- 110 columns and four spaces per level
- braces attached, and on every control statement
- namespace bodies indented, with no comment after the closing brace
- one argument per line when a call doesn't fit on one line
- pointers and references bound to the type (`const Quat& q`)
- the include order: the file's own header, `<windows.h>`, other `.h` headers in angle brackets, the
  standard library, then the project's headers
- Python's layout, with ruff's defaults (double quotes)

What they don't do, so it is done by hand:
- **A braced list that doesn't fit on one line** gets a comma after its last item. clang-format then
  puts one item per line.
- **A call whose result is checked** goes in its own statement when it doesn't fit on one line with the
  check (the `D3D11CreateDevice` example below).
- **HLSL inside a C++ raw string** isn't formatted. Lay it out like `warp.hlsli`.
- **Batch, PowerShell and response files** have no formatter ("Batch, PowerShell and response files",
  below).

## Paragraphs

The formatter can't see what belongs together. Code reads in paragraphs.

- **Group statements by step.** A paragraph is one step of what the function does, such as reading
  the settings, making the device or writing one line of the log. Decide what belongs together. Never
  bunch unrelated kinds of statement into one paragraph because they happen to be next to each other.
- **A blank line before and after every multi-line statement:** a control statement with its block,
  a wrapped call, a lambda, a braced list. Not at the start or the end of a block, and not before what
  goes on with the statement (`else`, `catch`).
- **Each guard is its own block.** A check that leaves the function has its braces and its body on its
  own lines and a blank line after it. The next step starts its own paragraph.
- **A comment that starts a paragraph** sits right above the paragraph's first statement. The blank line
  goes above the comment.
- **A `return` after other steps** is its own paragraph.
- Python follows the same rules.

Good (`PimaxSource::Start`), the layout the maintainer agreed:

```cpp
    if (!arrival_) {
        *error = "no arrival timing set (PimaxSource::SetArrivalTiming)";
        return false;
    }

    if (!session_.Open(error)) {
        return false;
    }

    poses_.SetPeriod(session_.Period());
```

Bad: declarations, guards and a loop of three different steps bunched together, with one-letter names.

```cpp
    char path[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("SIMOPTIX_FAKE_PVR_SCENARIO", path, MAX_PATH)) return;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return;
    char line[512];
    std::map<std::string, std::string> kv;
    while (fgets(line, sizeof(line), f)) {
```

Good (`fake_pvr::Config::Load`):

```cpp
    char path[MAX_PATH] = {};

    if (!GetEnvironmentVariableA("SIMOPTIX_FAKE_PVR_SCENARIO", path, MAX_PATH)) {
        return;
    }

    FILE* file = nullptr;

    if (fopen_s(&file, path, "r") != 0 || !file) {
        return;
    }

    std::map<std::string, std::string> values;
    char line[512];

    while (fgets(line, sizeof(line), file)) {
```

Bad: the call that does the work hides in a wrapped condition, and the `return` does two jobs at once.

```cpp
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device_, nullptr, &context_))) {
        return false;
    }
    ...
    D3D11_QUERY_DESC queryDesc = {D3D11_QUERY_EVENT, 0};
    return picture_.Init(device_.Get(), thumbTiles_ > 0) && SUCCEEDED(device_->CreateQuery(&queryDesc, &done_));
```

Good (`fake_pvr::Headset::CreateDevice`):

```cpp
    const HRESULT hr = D3D11CreateDevice(
        adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_,
        nullptr,
        &context_
    );

    if (FAILED(hr)) {
        return false;
    }
    ...
    if (!picture_.Init(device_.Get(), thumbTiles_ > 0)) {
        return false;
    }

    const D3D11_QUERY_DESC queryDesc = {D3D11_QUERY_EVENT, 0};

    if (FAILED(device_->CreateQuery(&queryDesc, &done_))) {
        return false;
    }

    return true;
```

## Names

The dev skill's "1b. Names" names the parts, files and concepts. Inside the code:

| What | Form | Example |
|---|---|---|
| Types, functions, methods | `PascalCase` | `PimaxSource`, `Report()` |
| Variables, parameters, struct fields | `camelCase` | `lastPoseSample`, `frame.serial` |
| Private and protected data members | `camelCase_` | `running_`, `stages_` |
| Constants and enumerators | `kName` | `kIdleAfter`, `kStopped` |
| Mutable variable in a file's anonymous namespace | `g_camelCase` | `g_headset` |
| Namespaces, files, folders | `lower_snake_case` | `stage_replay`, `pimax_source.cpp` |

- **A name says what a thing is or does.** A function that changes something starts with a verb
  (`Reset`, `Publish`). A question reads as one (`IsOpen`, `HasSignal`).
- **A number with a unit carries it** in the name, unless the type says it: `delayS`, `kIdleWaitMs`,
  `maxPoseErrorRmsDeg`. Times are seconds unless named otherwise.
- **Short names only where they are a convention:** `t` for a time, `dt`, `q` for a quaternion, `v` and
  `w` for vectors, `x`, `y` and `z`, `i`, `j`, `k` and `n` for loops and indexes, `hr` for an
  `HRESULT`, `r` for an `XrResult`, and the like. A name that is merely short gets a readable one: two
  keys are `fromKey` and `toKey`, not `a` and `b`, and a fraction is `fraction`, not `u`. So does an
  abbreviation a newcomer would have to look up.
- **No prefixes for types or scope** (`m_`, `s_`, `p`), except `g_` above.
- **Names fixed from outside stay as they are:** an export OBS or OpenXR looks up (`obs_module_load`),
  an SDK's types and callbacks, HLSL entry points, and a container that stands in for a standard one
  (`Trail` in `overlays\gmeter\meter_view.h`: `push_back`, `pop_front`, `size`).

## Comments

- **A comment says what the code can't:** why, a unit or a convention (m/s², degrees,
  performance-counter seconds, axis directions), a measured reason, a trap.
- **No boilerplate.** No comment that labels a group of statements, repeats a function's name or
  narrates what the next lines do. A function's name and parameters explain it. When a function needs
  a comment to be understood, fix the function.
- **Every header opens with a comment** after its copyright lines: what the file is and what it
  deliberately doesn't know (see `vr_mirror\core\pipeline\pipeline.h`). A `.cpp` with a header of the
  same name has none of its own. A program's main file and a `.cpp` with no header carry the full text.
- **A member that belongs to one thread says which:** `// graphics thread`.
- **Whole sentences above code** start with a capital letter and end with a full stop. A trailing
  comment is a lower-case phrase.
- **Only line comments.** A block comment is only for an unused parameter's name (`/*prefix*/`).
- **No commented-out code.** Git keeps it.
- **Moving code keeps its comments' wording.** Changing code updates the comment above it in the same
  commit.

## Other rules for C++

- **`#pragma once`** in every header. A header includes what it uses, and forward-declares a name that
  only appears as a pointer or reference.
- **Project includes** follow the dev skill: from the repo root for `shared\` and the frame sources,
  from `vr_mirror\core\` inside core and its hosts, and the plain file name in the same folder. An
  include whose order matters says why in a comment above it.
- **`using namespace`** only in a `.cpp`, and only for the part's own namespace.
- **`#define`** only where an SDK asks for it (`XR_NO_PROTOTYPES`, `NOMINMAX`).
- **Casts** use the named casts: `static_cast` for numbers and enums (`static_cast<double>(f.QuadPart)`),
  `reinterpret_cast` for pointers. No C-style casts (`(int)x`) and no functional casts (`int(x)`).
- **Literals:** `nullptr`, never `NULL`, and `0.5f` for a float.
- **One declaration of a pointer or reference per line.** Plain values of one type may share a line
  when they belong together (`double lastStep_ = 0, lastUpdate_ = 0;`).

## Python

For the scripts in `vr_mirror\recording_analysis\`, `.agents\skills\` and any other tool. They run as
`python -I script.py`.

- **Names follow PEP 8:** functions, methods, variables and parameters in `snake_case`, classes in
  `PascalCase`, constants in `UPPER_SNAKE_CASE`. The short names above apply here too.
- **Every file** starts with the two copyright lines as `#` comments and a module docstring that says
  what the script is and how to run it.
- **Imports** come in three groups with a blank line between: the standard library, third party
  (`numpy`), the repo's own modules. A script that imports its neighbours puts the `sys.path.insert`
  just above them.
- How a script is built (`main()`, no bare `except`) is in `code-practices`.

## Batch, PowerShell and response files

- **Batch files** (`build.bat`) start with `@echo off`, the copyright lines as `rem`, and a `rem`
  paragraph on what the script builds. A command that fails ends with `|| exit /b 1`. A long command
  breaks with `^`, its continuation indented three spaces, under 110 columns.
- **PowerShell** starts with the copyright lines as `#` and a comment on what it does. It sets
  `$ErrorActionPreference = 'Stop'` when it changes a system. Variables are `camelCase`.
- **Response files** (`*.rsp`) hold one path per line, in the order of the folder structure.

## Review checklist

- [ ] `clang-format --dry-run --Werror` and `ruff format --check` report nothing
- [ ] Statements grouped by step, with no unrelated kinds of statement bunched together
- [ ] A blank line before and after every multi-line statement, and each guard its own block
- [ ] A braced list that doesn't fit has one item per line, and a checked call that doesn't fit is its
      own statement
- [ ] Names say what a thing is. Short names only where they are a convention
- [ ] No comment that labels a group, repeats a name or narrates. Comments say why, give units and name
      the owning thread
- [ ] Every header opens with its comment. No commented-out code
- [ ] A rename touched every use, and the build lists
- [ ] The `code-practices` checklist passes too

---
name: code-practices
description: How to build SimOptix's code so it stays correct. Rules for functions, parameters, const, ownership and RAII, error handling across C APIs, threads and Python scripts, with examples from the repo and a review checklist. Use whenever code is written, changed or reviewed, together with the code-style skill.
---

# Building SimOptix's code

Use this whenever code is written, changed or reviewed. It is the second half of the code rules. The
`code-style` skill (`.agents\skills\code-style\SKILL.md`) says how code looks: paragraphs, layout, names,
includes, comments. This one says how code is built: functions, const, who owns what, what happens when
a call fails, which thread does what. The repo-wide rules are in `.agents\skills\dev\SKILL.md` ("Code
structure"). The terms of this repo (frame source, stage, host, core) are in
`.agents\skills\dev\reference\terms.md`.

The goal is code that does one thing at a time, owns what it opens, and fails in a way that cannot end
OBS or the game. The examples come from this repo.

## Functions

- **One job per function.** Split a function when it is hard to follow, not when it passes a line
  count. About 60 lines or 4 levels of nesting is a hint to look, not a rule. The paragraphs of a long
  function show where to split it, and a paragraph with its own introducing comment is the first
  candidate to become a function.
- **A split changes no output.** Compare `stage_replay --golden` byte for byte before and after, as the
  dev skill asks for a refactor ("Keep every stage swappable").
- **Guard clauses first.** Return early for the cases that stop the function, then the main path stays at
  the left. No `else` after a `return`.

  ```cpp
  bool PimaxSource::Start(std::string* error) {
      if (!arrival_) {
          *error = "no arrival timing set (PimaxSource::SetArrivalTiming)";
          return false;
      }

      if (!session_.Open(error)) {
          return false;
      }

      ...
  ```
- **Parameters:**
  - A number, a pointer or a small struct goes by value.
  - A larger object that is only read goes by `const&`.
  - An object the function changes goes by `&`.
  - A result the caller may skip, or a value the function fills, goes by pointer (`std::string* error`,
    `Quat* pose`).
  - A function with many outputs returns a struct instead. A function with more than about 5 parameters
    takes a struct (`OutputRequest`).
  - Never pass a `std::string` or `std::vector` by value, unless the function keeps it (then move it).
- **A function that can fail returns `bool`** and puts the reason in `std::string* error` when the caller
  can use it.
- **Short bodies go in the header** (accessors, one-line wrappers). Everything else goes in the `.cpp`, in
  the order of the declarations.
- **File-local helpers go in the anonymous namespace,** not as `static` functions.
- **A lambda is a short local helper** (up to about 10 lines). A thread's entry point is a lambda that calls
  one named member function and catches everything (see "Error handling").
- **A function reads what it needs from its parameters.** A script or program that reads a variable of its
  own `main()` from a helper function passes it in (`StepError(T, I, V, point, scale, moved)`).
- **Hot paths** (per frame, per tick) allocate nothing, take no blocking lock and make no call that can wait
  (dev skill, "The game and the Pimax compositor come first").

## Const-correctness

- **A local that is not changed after its first value is `const`.** Use `const` for scalars, strings and
  results of calls.

  ```cpp
  size_t len = strlen(message);          // bad: never changes
  const size_t len = strlen(message);    // good
  ```
- **A member function that leaves the object as it is, is `const`.**
- **A pointer or reference parameter that is only read is `const`.**
- **No `const` on a by-value parameter** in a declaration. It says nothing to the caller.
- **`constexpr` for values known at build time,** and `static constexpr` for a class's constants.
- **`mutable` only for a lock or a cache,** with a comment that says which.
- **No `const_cast`.** Fix the type that made it seem needed.
- **What an API changes through a reference stays non-const.** `compare_exchange_strong(expected, ...)`
  writes to `expected`, so that local is not `const`.

## Ownership and RAII

- **A resource has one owner,** and the owner releases it. The release is in a destructor, never at the end
  of a function that has early returns.
- **Use the type that releases for you:**

  | Resource | Owner |
  |---|---|
  | Heap object | `std::unique_ptr` (`std::shared_ptr` only where two owners truly outlive each other) |
  | COM object | `Microsoft::WRL::ComPtr` |
  | Lock | `std::lock_guard` or `std::unique_lock` |
  | Thread | a `std::thread` member, joined in `Stop` and in the destructor |
  | Win32 handle (event, file mapping, process, timer) | `UniqueHandle` (`shared\unique_handle.h`) |
  | Mapped view of a file mapping | `MappedView<T>` (the same header) |

- **A raw pointer does not own.** A function that takes a raw pointer or a reference neither keeps it nor
  frees it. Ownership moves only as a `std::unique_ptr`.
- **A pointer that passes through a C API as `void*`** (an OBS source's data) is made with
  `std::make_unique` and given away with `release()` in `create`. It is taken back into a `unique_ptr`
  in `destroy`. That is the one place that frees it (`vr_mirror\obs_plugin\plugin.cpp`).
- **No other `new` or `delete`,** except an object that must live until the process ends
  (`static Reader* reader = new Reader();`). A comment says why: so it is not destroyed while a DLL unloads.
- **No `malloc` or `free`.**
- **A Win32 call that writes a handle** (`DuplicateHandle`) writes into `UniqueHandle::Receive()`.
- **By hand, on purpose:** a handle the code gives away or ends in one function (the process handles of a
  program the e2e runner starts and terminates) and the mapping that lives for the process
  (`shared\shared_memory.cpp`). A comment says so.
- **A class with RAII members declares no copy, move or destructor** (the rule of zero). A class that owns a
  thread or a handle and declares a destructor deletes its copy.
- **No globals** except file-local variables in an anonymous namespace (dev skill, "Small interfaces").

## Error handling across C APIs

- **Check every call that can fail.** `FAILED(hr)` for `HRESULT`, `XR_FAILED(r)` for `XrResult`, the result
  of every Win32 call that returns a handle or `BOOL`, and every pointer a C API returns. Then take the
  failure path. A call whose result is skipped on purpose is cast to `(void)` with a comment that says why.
- **Log a failure once,** with the call's name and its code, where it is first known. Do not log it again
  on every frame (dev skill, "Logging").
- **Report to the caller with a return value,** as `bool` with `std::string* error`, or a struct. Exceptions
  are not used for control flow.
- **No exception crosses a C API or a thread.** Each of these catches everything at its outermost function,
  logs and returns cleanly:
  - a thread's entry point
  - an OBS callback
  - an OpenXR layer function
  - a window procedure
  - an exported function
- **A callback that a C program calls runs its body through `Guarded`** (`shared\guarded.h`). It catches
  everything, logs once for that call site and returns the empty answer (nothing, 0, `nullptr`, `false`).

  ```cpp
  void VideoTick(void* data, float) {
      Guarded("tick", [&] { ViewOf(data)->Tick(); });
  }
  ```
- **A thread's entry point is a lambda with `try` and `catch (...)`:**

  ```cpp
  thread_ = std::thread([this] {
      // An exception escaping a thread would end OBS on the spot; log it and stop copying.
      try {
          Run();
      } catch (...) {
          CoreLog("copy thread stopped by an unexpected error");
      }
  });
  ```
- **Release what the failed step acquired.** With owners (above) a failure path needs no cleanup code.
- **Text goes through `snprintf` with the buffer's `sizeof`,** never `sprintf` or `strcpy`. Logs use
  `CoreLog` (dev skill, "Logging").
- **Sizes and counts** are `size_t` or `uint64_t`. A comparison between signed and unsigned values is cast
  on purpose, because `/W3` warns about it.

## Threads

- **A comment says which thread owns each member** (code-style skill, "Comments").
- **Threads share only through `std::atomic` or a briefly held mutex.** The game, Pimax and OBS's render
  thread never wait on a lock: try the lock and skip (dev skill, "The game and the Pimax compositor come
  first").
- **A thread is started in `Start`, stopped and joined in `Stop`,** and `Stop` is safe to call twice.
- **Time and inputs come in as parameters.** Logic that decides things reads no clock and calls no
  Win32 or D3D, so `stage_replay` can drive it (dev skill, "Logic apart from I/O").

## Python scripts

- **Work in functions.** A script that runs when started ends with `main()` and the guard. Importing a
  module runs nothing but definitions and constants.

  ```python
  if __name__ == "__main__":
      sys.exit(main())
  ```
- **No bare `except:`,** no `from x import *`, no default argument that is a list or a dict.
- **Files** are opened with `encoding="utf-8"`. Paths come from arguments or `os.path.join`, never typed
  with a user's name.
- **A function that is only a helper takes what it uses as parameters** and does not read a variable that
  `main()` made.
- **A script that rewrites files changes the same syntax tree** when it is a layout change: compare
  `ast.dump` of the old and new source.

## Review checklist

Read every changed line. No script checks these.

- [ ] Each function has one job and is easy to follow
- [ ] Early returns for the failure cases, no `else` after `return`
- [ ] Parameters: `const&` to read, pointer for outputs, a struct past about 5
- [ ] `const` on every local, member function and parameter that allows it
- [ ] Every resource has one owner that releases it, with no `new` or `delete` outside the two cases
- [ ] No `HANDLE` or mapped view closed by hand where a `UniqueHandle` or `MappedView` fits
- [ ] Every call that can fail is checked
- [ ] Every thread entry point and every callback a C program calls catches everything
- [ ] Each thread-owned member says which thread owns it
- [ ] A change that must not alter behaviour builds every target with no new warnings and keeps
      `stage_replay --golden` byte for byte the same
- [ ] Python: functions and a `main()` guard, no bare `except`

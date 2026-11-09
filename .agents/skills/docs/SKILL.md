---
name: docs
description: Writing SimOptix's documentation. Use whenever a README, CONTRIBUTING.md, AGENTS.md, a file of the skills in .agents\skills\, or a pull request's Changelog lines are written or changed.
---

# Writing SimOptix's documentation

Use this whenever a README, `CONTRIBUTING.md`, `AGENTS.md`, a file of the skills (`.agents\skills\`),
or the Changelog lines of a pull request are written or changed. The maintainer's priorities: plain English at the reader's level, short, precise,
and only what exists.

Legal text is exempt from every rule here: `LICENSE`, `THIRD_PARTY_NOTICES.md`, the notice under
"Licence" in the root `README.md`, and the files in `third_party\`. Code and code comments are not
documentation. They follow the dev skill's rules.

## State the facts, plainly

This comes before every other rule. A sentence states facts in the plainest words that state them.
It doesn't try to sound good.
- **List the facts first, then write them down.** For the root README's opening the facts are: what
  SimOptix is, who it is for, that it comes as OBS Studio plugins, what each plugin adds, and what
  they work with. The sentence holds those and nothing else.
- **Use the plain verb.** "VR Mirror adds sources", not "brings the headset view into OBS". "The tools
  are available as OBS Studio plugins", not "it comes as". No metaphors, no smoother synonyms.
- **Name things.** Say "the tools", "VR Mirror", "the G-meter", not "it" or "this" when the reader
  would have to work out what is meant.
- **Use the reader's own terms.** An OBS plugin adds a **source**. Pimax Play's setting is **Render
  Quality**. Never write around a word the reader uses every day.
- **The rules below serve this one.** If applying a rule makes a sentence vaguer, the rule is being
  misapplied. Avoiding jargon never means avoiding a word the reader knows. A product description never
  means adding how the product behaves.

The root README's opening, as the maintainer wrote it, is the model:

> SimOptix is a set of sim racing tools for capturing stabilized VR footage and overlays.
> The tools are available as OBS Studio plugins: VR Mirror adds sources with the stabilized headset
> view, and Overlays adds a G-meter source. Both work with Le Mans Ultimate (LMU) and Pimax headsets.

## The maintainer's wording and edits

- **Wording the maintainer gives is used as it is.** Fix only a plain typo, and say which.
- **When the maintainer points at a phrase, change that phrase only.** Don't rewrite the sentence or
  the paragraph around it. Show the result before changing anything else.
- **Before changing a passage the maintainer has edited, read its history** (`git log -p`). Don't put
  back what the maintainer took out.
- **Getting it right is the writer's job, not the maintainer's.** Don't hand the work back by asking
  for wording, or by offering versions to pick from. List the facts, check them in the code and the
  history, and write the plain sentence. Ask only about a fact that the repo and its history can't
  answer.

## Who reads what

People come to this repo because they play VR sim racing games. They know VR, their headset and its
software, OBS, and what stabilized footage is and why raw headset footage needs it. Many have used
OpenXR tools and API layers such as OpenXR Toolkit. Write for that reader. Plain doesn't mean dumbed
down: never explain what this reader already knows, and never talk down to them.

Docs get more technical the deeper they sit. Each level may assume everything the level above it
assumes.

| Level | Reader | What it may assume, without explaining |
|---|---|---|
| Root `README.md` | A VR sim racer who found the repo | VR, headsets, Pimax Play and its Render Quality, OpenXR and API layers, OBS and its sources, Le Mans Ultimate (LMU), Easy Anti-Cheat, frame rates, stabilization. No code names and no design talk |
| `CHANGELOG.md`, and the Changelog section of each pull request | The same reader, deciding whether to update | The same as the root README ("The changelog", below) |
| Plugin READMEs (`vr_mirror\obs_plugin\`, `overlays\`) | That reader, setting the plugin up and using it | The same, plus OBS's own settings and log. Settings by their on-screen names. Their diagnostics sections may be technical |
| Part READMEs (`vr_mirror\core\`, `vr_mirror\frame_sources\`, `shared\telemetry\`, `vr_mirror\e2e\`, ...) | A contributor who reads the code | C++, Windows, Direct3D 11, shaders, quaternions, swapchains, shared memory, threads. Defines the terms coined here |
| Stage and detail READMEs (`vr_mirror\core\pipeline\<stage>\`, `overlays\gmeter\design\`) | A contributor changing that part | The parent README too |
| `CONTRIBUTING.md` | A first-time contributor | Git, a C++ toolchain, and the root README |
| `HOW_SIMOPTIX_IS_BUILT.md` | Anyone who wants to know how SimOptix is built | The same as `CONTRIBUTING.md` |
| Skills (`.agents\skills\`: each `SKILL.md`, the dev skill's `reference\` and `findings\`) | The maintainer, contributors and AI agents working on the repo | Everything above, and the terms in `reference\terms.md`. Technical, but still clear and structured |

## Rules

1. **Start with what the thing is and does.** The first paragraph of every README says what the part
   is for, to the reader of that level. The root README starts with a product description with almost
   no technical jargon. No tagline, no noun stack.
   - The root README's opening says what SimOptix is and what its parts are, and nothing more ("State
     the facts, plainly", above). How a part behaves (the horizon held level, the G-meter in step with
     the video) goes under "What you get" or in that part's README.
2. **Define a term before its first use, in a terms table, when the reader may not know it.**
   - Every term coined in this repo counts: "arrival slot", "display slot", "drawn pose", "display
     grid", "slot check" and the like. They are in `.agents\skills\dev\reference\terms.md`, "Terms
     coined in this repo".
   - An outside term counts when the reader of that level may not know it ("client mirror", "PVR",
     "performance counter" in a part README). `terms.md`, "Outside terms", lists them.
   - Never define what the reader of that level knows: VR, headset, OpenXR, OBS, LMU, Easy Anti-Cheat
     and Render Quality for everyone, and C++, shaders and quaternions for contributors. A table of
     basics reads as if the reader had never heard of VR.
   - A coined term that is also a name OBS shows, such as Look-ahead (a choice of VR Mirror's
     Smoothing), needs no table where the text uses it as that name. The root README and
     `CHANGELOG.md` describe it as a setting, in the sentence that names it. A table there would be
     too low level for their reader. `check_docs.py` lists these names in `ON_SCREEN_NAMES`.
   - The table is `| Term | Meaning |`, after the opening paragraph and before the first use. It
     holds only the terms that document uses. Every README defines the coined terms it uses, even if
     a parent README defines them too. Dev skill files link to `reference\terms.md` in their first
     lines instead.
   - The opening paragraph may use a term only if it explains it in the same sentence.
   - Once a table defines a term, the text doesn't explain it again.
   - A new term goes into `reference\terms.md` and into the table of every README that uses it.
   - One word, one meaning, everywhere. Never two names for one thing.
3. **Describe only what exists.**
   - When describing the code, say what it does, not what it doesn't do: no "doesn't do X", "never
     does Y", "no longer", "unlike before". Those are leftovers of development decisions.
   - These are not such leftovers, and stay:
     - rules and prohibitions for contributors ("Don't inject code into the game", "Never rename a
       setting key")
     - a stage's "Must not know" line, which the dev skill asks every stage README for
     - warnings ("Don't install the layer on a PC used for online racing")
     - facts about other software that the reader needs ("The Pimax runtime has no size option for
       its mirrors")
     - measured limits that matter to the reader, said as plain facts ("VR Mirror stabilizes the
       head's rotation. Leaning stays in the view as the game drew it")
     - what was not checked ("Not checked in a recording"). Never leave it out to sound sure
   - If the reader needs to know what the software works with, say it as a plain fact: "Works with
     Pimax headsets and Le Mans Ultimate". Don't say it as a limit: "Only Pimax is supported".
   - No history in READMEs: no dates of changes, no "was replaced", "used to". History lives in
     commit messages, and for users in `CHANGELOG.md` ("The changelog", below). Findings files keep
     dated measurements and dead ends, because those are measured facts.
   - No plans, roadmap codes, "for now", "yet" or "being obtained".
4. **Cut what gives the reader nothing.** Count words ("two plugins", "in one repo"), "simply",
   "just", "note that", "as you can see", a sentence that restates its heading, and any statement
   that stays true for every project.
5. **No semicolons.** Split the sentence, or use a bullet list. Avoid colon chains and nested
   parentheses too.
6. **Concise, never at the cost of readability or nuance.**
   - Full sentences with a verb. No headline style, no item-name phrasing.
   - One idea per sentence, about 25 words at most.
   - If a sentence needs reading twice, rewrite it.
   - Say what happens, in the reader's terms, and give an example when a rule is abstract.
   - Shortening keeps the facts that make a statement true: its numbers and units, its conditions
     ("at Render Quality 1.0", "with Look-ahead"), its reasons ("because ..."), and its exceptions.
     Remove words, never these.
7. **No code names in text for non-programmers.** A code name is the name of a class, function,
   file or setting key, such as `PimaxSource` or `kDrawnPose`. In the root README and the
   plugin READMEs, say what the thing is instead: "the Pimax source", "the Smoothing setting". Give a
   file path only where the reader has to open that file. In READMEs for contributors, code names
   go in tables, next to a plain description.
8. **Avoid "picture" as a technical word.** Use the precise one:
   - **frame**: what the game draws for the headset, and its record
   - **eye image**, or **image**: the image of one eye in a frame, or any other image
   - **view**: what VR Mirror shows
   - **video**, **recording**, **stream**: what OBS outputs

   The code's own `picture_*` names (the picture room, `FakeGamePicture`) stay where they name that
   code. Log lines are quoted as the code writes them.
9. **Facts first.**
   - Check a claim against the code or a measurement before writing it. Say what was not
     verified.
   - A number carries its unit and, in findings, its conditions (headset, scenario, date). The date
     is the day of the work on the code. A driving session or a recording carries no date.
   - Docs are public. Nothing personal goes in them: no secrets, tokens or values of environment
     variables, no local paths or machine names, and no recording names, session dates or times,
     tracks or cars. The public-text skill (`.agents\skills\public-text\SKILL.md`) has the full list
     and the check.
   - Keep every fact when rewriting. Remove fluff, not information.

10. **The same fact in the same words, for every variation.** Wherever the repo has more than one
    variation of something, a fact that holds for more than one of them is written the same way for
    each. Examples are the two plugins, the two sources, the frame sources, a stage's implementations,
    and the kinds of file in `%LOCALAPPDATA%\simoptix`.
    - Only the variation's own name or value changes ("The OBS log marks this plugin's lines
      `[simoptix_vr_mirror]`" and "... `[simoptix_overlays]`").
    - When one variation's doc states such a fact, the others' docs state it too, in the matching
      place (the Install section of each plugin README, say).
    - Change all of them in the same commit.

## Formatting

- **Headings:** sentence case, naming the content ("Settings", "Files"), not a teaser.
- **Tables** for reference: terms, files, settings, measures, options. A cell holds a phrase or
  two sentences. Longer explanations go in the text under the table.
- **Bullets** for parallel items, requirements, and what-it-checks lists. **Numbered lists** only
  for steps in order.
- **Prose** for explanations that have a cause and an effect.
- **Code blocks** for commands, file contents and log lines. Paths and names in backticks.
  One command per block when the reader will run it.
- Paths use backslashes, as the repo does. Relative links resolve from the repo's root.
- Write units with a space (`0.5 s`, `90 fps`, `2 GB`). Name the setting, not the key, when the
  reader sees it in OBS. Give the key in a table when the reader edits a file.
- Order inside a README: description, status or limits, terms, what it does or how to use it,
  settings, files.

## The changelog

`CHANGELOG.md` is the one document that holds history. It is written for users: the root README's
reader. It has one section per release, newest first, in the Keep a Changelog format.

- **Its lines are written in the pull request,** in the Changelog section of its description, with
  one label for their heading: `added`, `changed`, `fixed`, `removed` or `security`. The release
  script copies them into `CHANGELOG.md` at a release. Don't edit `CHANGELOG.md` in any other pull
  request.
- **Every change users notice gets lines, in the same pull request,** the way a README moves with the
  code. Users notice what they see, set, install or run, and how well it works. A pull request whose
  change only contributors notice is labelled `internal` and has none: tests, refactors,
  contributors' docs, the skills, the build, findings.
- **One to three lines per pull request,** each a bullet (`- `) with one change. Say what changed for
  the user, with its numbers and conditions, in the words of the root README. Use the names the user
  sees: the sources, the settings, the files they run or open.
- **The rules above apply,** with three changes. A line says what is new or different, because that
  is its job. It never says how the code did it. It gives no pull request numbers or commit names.

| Bad | Why | Good |
|---|---|---|
| Build: each DLL carries the version in its version resource | A commit subject, for developers | Windows shows SimOptix's version in each plugin's Properties, under Details |
| Fixed the install | Doesn't say what was wrong, or for whom | `build.bat install` makes the plugin's folders on a PC that has none |
| Refactored the frame ring into its own file | Users notice nothing. Label it `internal`, with no lines | (no line) |

## Findings and reference files

The dev skill's files are read by people and agents in the middle of work. They need the facts fast.
- **Findings** (`findings\<part>.md`): one section per subject. Inside it, one bullet or short
  paragraph per finding, with what was measured or tried, the numbers with their conditions, the
  date, and the conclusion. A recording or a driving session is named by what it shows or by a
  letter, never by its file name, date, track or car (public-text skill). A dead end says why it was dropped, with the measured reason. Keep the
  history a measurement needs ("before the fix: 1.6 %, after: 0 %"), and nothing else.
- **Reference** (`reference\*.md`): procedures as numbered steps, options and file formats as
  tables, and the reasons a step exists.

## Examples

| Bad | Why | Good |
|---|---|---|
| Two OBS plugins, in one repo. | "Two" and "in one repo" tell the reader nothing | SimOptix adds sources to OBS Studio |
| Neither needs the other: each builds, installs and runs alone. | States a design decision, and what the code doesn't do | (delete) |
| Stabilized VR footage of sim racing for recording and streaming | Reads like an item name | SimOptix is a set of sim racing tools for capturing stabilized VR footage and overlays |
| It works as plugins for OBS Studio, the free program for recording and streaming video | Explains OBS to people who use it | It works as plugins for OBS Studio |
| Its Overlays plugin draws a G-meter over the video, in step with it (in the root README's opening) | A detail of how a part behaves, in the opening | The tools are available as OBS Studio plugins: VR Mirror adds sources with the stabilized headset view, and Overlays adds a G-meter source |
| It comes as OBS Studio plugins | "It" and "comes as" leave the reader to work out what is meant | The tools are available as OBS Studio plugins |
| Its VR Mirror plugin brings the headset view into OBS Studio | Writes around the OBS term the reader knows | VR Mirror adds a source with the stabilized headset view |
| A terms table with "LMU: Le Mans Ultimate" and "OpenXR: the open standard that a VR game uses to talk to the headset's software" | Defines what every VR sim racer knows | (no row) |
| OpenXR calls this kind of add-on an "API layer": it sits between the game and the runtime | Explains what the reader knows | An API layer that OpenXR loads into LMU |
| Then Gauss-Newton fitting, which refines the move step by step, runs on both patches | Explains a standard method to a contributor | Then a Gauss-Newton fit runs on both patches |
| Pimax Mirror takes the game's view from Pimax's software | Loses the fact that matters: the size | Pimax Mirror takes the half-size copy of the left eye that Pimax Play makes (2508×2084 in LMU at Render Quality 1.0) |
| Depends on: plain data | Replaced the stage's "Must not know" rule, which the dev skill asks for | **Must not know:** PVR calls, threads, the frame's image, or anything later in the pipeline |
| Two sources, the same but for where their picture comes from | Confusing | Two sources show the same view. They differ in where the game's image comes from |
| Both read the game's telemetry through one reader for the whole of OBS | Internal design in the root README | The G-meter and race auto-record read the same game data |
| `The delay costs memory; Look-ahead needs 1.1 GB.` | Semicolon | The delay costs video memory. Look-ahead needs 1.1 GB. |
| The slot check re-times a frame to its arrival slot | Coined terms undefined | Define "slot check" and "arrival slot" in the terms table first, then use them |
| It never makes the game wait. | Describes the code by what it doesn't do | It copies each frame after the game has sent it to the headset. |
| For now the layer works only offline | A plan word, and a limit | The layer works in LMU started without Easy Anti-Cheat, which means offline |

## Checklist before committing docs

Run the check script from the repo's root:

```
python -I .agents\skills\docs\check_docs.py
```

It checks the items marked "(script)" and lists the lines to review for those marked "(script lists
them)". A clean run is not the end. Read the rest yourself.

- [ ] Each changed sentence states its facts in the plainest words: plain verbs, things named, the
      reader's own terms ("State the facts, plainly")
- [ ] Only what the maintainer pointed at changed, and their wording is used as given
- [ ] No semicolons outside code blocks, inline code and legal text (script)
- [ ] Every relative link and every repo path in backticks resolves (script)
- [ ] Every coined term of `reference\terms.md` has a row in a terms table at or before its first
      use (script)
- [ ] `CHANGELOG.md` has the release script's form: each section `## [x.y.z] - YYYY-MM-DD`, newest
      first, with its link, and only the groups Added, Changed, Fixed, Removed and Security (script)
- [ ] Outside terms used without a table row: needed at this level? (script lists them)
- [ ] "doesn't", "never", "no longer", "not yet", "for now" lines reviewed against rule 3 (script
      lists them)
- [ ] The first paragraph says what the thing is, for the reader of that level
- [ ] No row or sentence explains what the reader of that level already knows
- [ ] The root README has no code names and no design talk
- [ ] No statement of what the code doesn't do. No history. No plans
- [ ] No sentence over about 25 words, no headline-style fragments
- [ ] No fluff: count words, "just", "simply", restated headings
- [ ] Every fact of the old text survives, with its numbers, conditions and reasons, and every new
      fact was checked
- [ ] Nothing personal: `check_public_text.py --staged` run, and its lines read (public-text skill)
- [ ] A new term is in `reference\terms.md` and in each README's table that uses it
- [ ] A fact shared by several variations (both plugins, both sources, the kinds of file) reads the
      same for each, and every variation's doc states it
- [ ] The matching README, `CONTRIBUTING.md` and the dev skill changed together (dev skill, "Docs move
      with the code")
- [ ] A change users notice has one to three lines in its pull request's Changelog section and one
      type label. Any other pull request is labelled `internal` ("The changelog")
- [ ] The commit subject is `Docs: what changed`

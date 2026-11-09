# Versions, the changelog and releases

Terms: `reference\terms.md`. SimOptix has one version for all its parts, a changelog for its users,
and releases on GitHub that tie them together. This file has what carries the version, when to bump
it, how a pull request feeds the changelog, and the steps of a release.

## What carries the version

`VERSION` at the repo's root holds the version as major.minor.patch (SemVer), starting at 0.1.0.
`shared\build\version.bat` reads it for every build that makes a shipped file.

| Build | Version |
|---|---|
| Exactly on the release tag `v<version>`, nothing changed | `0.2.0` |
| Commits after the newest release tag | `0.2.0+12.gabc1234`: 12 commits after the tag, at commit `abc1234` |
| Before the first release tag | The same, with the commits counted from the first |
| Changes to tracked files that aren't committed | `.dirty` added: `0.2.0+0.gabc1234.dirty` |
| A copy of the repo without git | `0.2.0+unknown` |

The build stops when the commit carries the release tag of another version, such as `v0.1.0` on a
commit whose `VERSION` says 0.2.0.

| Where | What it shows |
|---|---|
| Each DLL's version resource (`shared\build\version_info.rc`) | The file version `major.minor.patch.<commits since the tag>` and the full string as the product version, in the file's Properties, under Details |
| Each plugin's `manifest.json` (`build.bat` writes it into `bin\`, `build.bat install` copies it to the plugin's `data` folder) | The full string as `version`. OBS 32.2.2 keeps it and shows it nowhere (`findings\obs_plugin.md`, "OBS's plugin manifest") |
| The layer's manifest in `bin\` | `implementation_version`, one decimal number: major × 1000000 + minor × 1000 + patch, so 0.2.0 is 2000. The OpenXR loader skips a layer whose number isn't a whole number |
| The OBS log | `[simoptix_vr_mirror] loaded, version 0.2.0`, and the same for `[simoptix_overlays]` |
| The layer's log | `XR_APILAYER_SIMOPTIX_frame_source 0.2.0 loaded into Le Mans Ultimate.exe, ...` |
| SimOptix Setup (`setup\`) | Its own version in the exe's version resource, after its window's title, and as `DisplayVersion` in its entry in Windows' installed apps. Each row shows the installed part's version, read from that DLL's version resource |

The version names a build for people, and decides nothing at run time. Whether parts fit together is
checked by the layout versions of the memory they share, which change only with that layout
(`openxr_share::kVersion` and `TelemetryArea::kVersion`).

## When to bump

Bump the version only in a release's pull request, by what changed for users since the last release.

| Bump | When | Example |
|---|---|---|
| Patch, 0.2.0 to 0.2.1 | Fixes only. Nothing a user has to do or set differently | `build.bat install` makes the plugin's folders on a PC that has none |
| Minor, 0.2.0 to 0.3.0 | Something added or changed. While the major is 0, also anything that breaks | A new setting. A setting removed or with a new meaning. A plugin and the layer that must be updated together, because their shared memory's layout changed. A new OBS version needed |
| Major, 0.x to 1.0.0 | When the maintainer calls SimOptix stable. After that, any change that breaks | |

## The changelog, from each pull request

`CHANGELOG.md` is the one document that holds history, written for users: the root README's reader,
a VR sim racer (docs skill, "The changelog"). It changes only at a release, by the release script.

Its lines come from the pull requests. Each one carries exactly one type label:

| Label | The pull request | Its description's Changelog section |
|---|---|---|
| `added` | Adds something users notice: a source, a setting, a game, a headset | One to three lines, each starting with `- ` |
| `changed` | Changes what users see, set or install, or how well it works | The same |
| `fixed` | Fixes something users noticed | The same |
| `removed` | Removes something users had | The same |
| `security` | Fixes a security problem | The same |
| `internal` | Changes nothing users notice: tests, refactors, contributors' docs, the skills, the build, findings | Empty |

A change to a README that users read, such as a corrected install step, is `changed`. A release's own
pull request is `internal`. A pull request with changes of two types takes the type of the one users
notice most, or is split in two. The template (`.github\pull_request_template.md`) has the section. An
agent opens the pull request with `gh pr create --label <label> --body-file <file>`.

`.github\release.yml` leaves the `internal` pull requests out of the list of merged pull requests
that GitHub can add under a release's notes.

## The release script

`.agents\skills\dev\release.py` writes the release's section of `CHANGELOG.md`. It needs the GitHub
CLI (`gh`), signed in.

```
python -I .agents\skills\dev\release.py
```

It takes the commits after the newest release tag up to `HEAD`, on the first-parent line, and the pull
request that each one merged. It puts the Changelog lines of each user-facing pull request under its
label's heading, in the order they were merged. Then it writes the section above the newest one, with
its link, and prints it.

It refuses and changes nothing when:
- a pull request has no type label, or more than one
- a user-facing pull request has no Changelog lines, more than three, or a line that isn't a bullet
- an `internal` pull request has Changelog lines
- no pull request has a change for users
- `CHANGELOG.md` already has a section for the version in `VERSION`, or `VERSION` isn't after the
  newest release tag

It lists the commits that came from no pull request and leaves them out.

| Option | What |
|---|---|
| `--dry-run` | Print the section, and change nothing |
| `--since <tag>` | Start after another release, for notes that cover several releases |
| `--date YYYY-MM-DD` | The release's date, today without it |
| `--prs <file>` | Read the pull requests from a file that `gh pr list --state merged --json number,title,body,labels,mergeCommit,url` wrote |
| `--notes` | Print the notes of the version in `VERSION` from `CHANGELOG.md` again, and change nothing |
| `--notes-file <file>` | Also write the notes to a file, in UTF-8. PowerShell's `>` would write UTF-16 |

## Making a release

Work in a worktree from `main` (`SKILL.md`, step 0), named `release_v<version>`.

1. Check the pull requests since the last release:
   `python -I .agents\skills\dev\release.py --dry-run`. Fix each one it lists on GitHub: its label,
   or the Changelog section of its description. Then choose the bump ("When to bump").
2. Write the new version into `VERSION`. Run `python -I .agents\skills\dev\release.py`, and read the
   section it wrote.
3. Commit `VERSION` and `CHANGELOG.md` as `Release: v<version>`, and open the pull request with the
   same title and the label `internal`. Merge it after its checks.
4. Tag the merged commit on GitHub's `main`, annotated, and push the tag:

   ```
   git fetch origin
   ```

   ```
   git tag -a v<version> origin/main -m "SimOptix <version>"
   ```

   ```
   git push origin v<version>
   ```

   Check first that the newest commit of `main` on GitHub is the release's, with the new `VERSION`.
5. Build the layer from the tag in a new worktree, so the version is exactly the release's
   (`git worktree add .claude/worktrees/build_v<version> v<version>`, then
   `vr_mirror\frame_sources\openxr\build.bat`). Each build prints `SimOptix <version>`, with no `+`.
6. Sign the layer with the user's own signing skill (`reference\openxr.md`, "Signing"). The version
   resource changed the DLL, so the release's layer is signed even if an earlier build was. Run its
   check program before the layer goes into the game.
7. Build SimOptix Setup, `setup\build.bat`. It builds both plugins too, and carries them and the
   signed layer. It ends with "The OpenXR layer in setup is signed (Valid)." and no warning. The PC
   needs LMU's headers, or the plugins read no game. Then sign `setup\bin\simoptix_setup.exe` the same
   way as the layer, with the same certificate, because Windows warns about a downloaded program
   that isn't signed.
8. Pack the built files, write the notes, and create the GitHub Release, all in a folder outside the
   repo:

   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .agents\skills\dev\release_files.ps1 <folder>
   ```

   ```
   python -I .agents\skills\dev\release.py --notes --notes-file <folder>\notes.md
   ```

   ```
   gh release create v<version> --title "SimOptix <version>" --notes-file <folder>\notes.md --generate-notes <folder>\simoptix_setup_<version>.exe <folder>\simoptix_obs_plugins_<version>.zip <folder>\simoptix_openxr_layer_<version>.zip
   ```

   `release_files.ps1` copies setup as `simoptix_setup_<version>.exe` and makes two zips. One holds
   both plugins, laid out as in OBS's plugin folder, each with its `manifest.json` in its own `data`
   folder. The other holds the layer and its manifest in `bin\`, beside `install.ps1`,
   `uninstall.ps1` and `uninstall.bat`. It refuses when a file's version isn't exactly the release's,
   or when the layer or setup isn't validly signed. `--generate-notes` adds GitHub's list of the
   merged pull requests under the notes, without the `internal` ones.

The first release, 0.1.0, is the exception to steps 1 and 2. The pull requests before it carry no
labels, and `VERSION` already says 0.1.0. Label them first, or write its section of `CHANGELOG.md` by
hand in step 2, saying what SimOptix 0.1.0 is. A section written by hand has the script's form,
with its link line at the end of the file, and `check_docs.py` checks it.

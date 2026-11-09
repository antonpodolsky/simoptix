# SimOptix Setup

SimOptix Setup, `simoptix_setup.exe`, installs and removes VR Mirror, Overlays and the OpenXR API
layer, and shows what is installed. It is one file that carries all three inside it, and needs nothing
beside it.

## Installing and removing

- Each row's buttons change that part alone. **Install all** (or **Update all**, once anything is
  installed) and **Uninstall all** change every part that needs it. They work only when none of those
  parts is blocked, so "all" always means all.
- **Install**, **Update** and **Repair** all install the part from setup. **Uninstall** removes
  everything of the part, from every place below. **Uninstall all** asks first.
- Windows asks for administrator rights when setup starts, because the layer goes into
  `C:\Program Files` and the registry for every user.
- Close OBS to change the plugins, and LMU to change the layer.
- Before it installs a layer that isn't signed with a certificate Windows trusts, setup asks first.
  Easy Anti-Cheat refuses such a layer: while it is installed, LMU started the usual way shows Easy
  Anti-Cheat's "Untrusted system file" warning, and online play is disabled. The layer then works only
  in LMU started without Easy Anti-Cheat. Its default answer is **Don't install**.
- **While a change runs,** the band names the part being changed and fills a bar, one step per part.
  Each row in the change says "Waiting", then turns a spinner with "Installing…" (or "Updating…",
  "Repairing…", "Uninstalling…"), then shows a tick with "Installed" (and so on), or "Failed". Every
  button waits until it ends, Close and the window's close box too, because a change can't stop
  halfway. The results stay 3 s, then the buttons come back.
- A message comes up only when something failed, with the reason.

## Where the parts go

| Part | Where |
|---|---|
| VR Mirror | `C:\ProgramData\obs-studio\plugins\simoptix_vr_mirror\`: the DLL, and `manifest.json` with the plugin's name and version in a folder named data |
| Overlays | `C:\ProgramData\obs-studio\plugins\simoptix_overlays\`, the same way |
| OpenXR layer | The DLL and its manifest in `C:\Program Files\simoptix\openxr_frame_source\`, registered in `HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` with the value 0 (on) |
| Setup's own copy | `C:\Program Files\simoptix\simoptix_setup.exe`, while anything is installed |

- **The plugins' folders depend on the OBS version.** OBS 32 and earlier load a plugin's DLL from its
  `bin\64bit\` folder. OBS 33 and later load it from the plugin's folder itself. Both read
  `manifest.json` from the folder named data. They flag a DLL in `bin\64bit\` as "Legacy", and OBS 34 won't load it. Setup installs into
  the folders of the OBS it finds, and removes the copy in the other folders. Only OBS 32.2.2 has been
  tried.
- **The layer goes where `vr_mirror\frame_sources\openxr\install.ps1` puts it.** Each one replaces and
  removes what the other installed. Installing also removes other registrations of the same layer,
  because OpenXR would otherwise find it twice.

## Windows' list of installed apps

While any part is installed, SimOptix is listed in Windows' Settings, under Apps, Installed apps, with
setup's version. Its **Uninstall** removes all three parts and the listing. It runs setup's own copy in
`C:\Program Files\simoptix`, which copies itself to the temporary folder to do the work, because a
running program can't delete its own file. Windows deletes that temporary copy at the next restart.

## Files

| File | What |
|---|---|
| `setup_main.cpp` | Starts setup: the window, or the removal with `/uninstall` |
| `setup_window.cpp` | The window, what its buttons do, and a change's progress |
| `setup_run.cpp` | Runs a change on a thread of its own, so the window keeps drawing its progress |
| `setup_look.cpp` | Where everything in the window goes, and its drawing, but the buttons |
| `setup_buttons.cpp` | The buttons' look: real Windows buttons, drawn by setup in each state |
| `setup_dialog.cpp` | Setup's questions, in its own look: the unsigned layer, and Uninstall all |
| `setup_theme.cpp` | The design in numbers: colours, sizes, fonts, and the title bar's colour |
| `setup_draw.cpp` | Smooth shapes and icons, with GDI+: the parts' glyphs, ticks, marks, the spinner |
| `setup_status.cpp` | Which buttons each row shows, and whether a part can be changed now |
| `setup_probe.cpp` | Reads what is installed, the layer's registrations, OBS and LMU |
| `setup_install.cpp` | Installs and removes each part, and keeps the entry in Windows' installed apps |
| `setup_uninstall.cpp` | The removal of everything that Windows' installed apps start |
| `setup_places.cpp` | Where each part goes, and the registry keys |
| `setup_parts.h` | The three parts, their DLLs and manifests, and the resources that carry them |
| `setup_version.h` | Setup's own version, from the build |
| `setup_facts.h` | What setup found on the PC, as plain data |
| `setup_payload.cpp` | The files inside the exe: finding, comparing and writing them |
| `setup_signature.cpp` | A file's code signature, by Windows' own check |
| `setup_registry.cpp`, `setup_system.cpp` | The registry, folders, processes, and the versions of OBS and of installed files |
| `setup.rc` | Puts the two plugins and the layer, with their manifests, into the exe, and gives the exe its icon and version resource |
| `make_icon.ps1` | Writes setup's icon, a placeholder logo, for each build |
| `setup.manifest` | Administrator rights, sharp text at any display scale, and Windows' current controls |
| `build.bat` | Builds both plugins and setup |

# SimOptix Setup

SimOptix Setup, `simoptix_setup.exe`, installs and removes VR Mirror, Overlays and the OpenXR API
layer, and shows what is installed. It is one file that carries all three inside it, and needs nothing
beside it.

## What it shows

Under its title, the window says which OBS Studio it found, and what to close before a part can be
changed: OBS for the plugins, LMU for the layer. Then it has a row for each part, with what the part adds
and the buttons that fit what is installed.

| Buttons | What is installed |
|---|---|
| **Install** | Nothing of the part |
| **Uninstall** | The same build as the one in setup, in its place |
| **Update** and **Uninstall** | A different build from the one in setup |
| **Repair** and **Uninstall** | The same build, out of place: a plugin in the folders of another OBS version, or a layer that is switched off, missing from OpenXR's list, missing its files, or registered from another folder too |

Setup reads the PC again whenever its window is activated. So after closing OBS or LMU, click the
window and the buttons follow.

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
  in LMU started without Easy Anti-Cheat.
- The buttons show the result. A message comes up only when something failed, with the reason.

## Where the parts go

| Part | Where |
|---|---|
| VR Mirror | `C:\ProgramData\obs-studio\plugins\simoptix_vr_mirror\` |
| Overlays | `C:\ProgramData\obs-studio\plugins\simoptix_overlays\` |
| OpenXR layer | The DLL and its manifest in `C:\Program Files\simoptix\openxr_frame_source\`, registered in `HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` with the value 0 (on) |
| Setup's own copy | `C:\Program Files\simoptix\simoptix_setup.exe`, while anything is installed |

- **The plugins' folders depend on the OBS version.** OBS 32 and earlier load a plugin's DLL from its
  `bin\64bit\` folder. OBS 33 and later load it from the plugin's folder itself, with a folder named data
  beside it. They flag a DLL in `bin\64bit\` as "Legacy", and OBS 34 won't load it. Setup installs into
  the folders of the OBS it finds, and removes the copy in the other folders. Only OBS 32.2.2 has been
  tried.
- **The layer goes where `vr_mirror\frame_sources\openxr\install.ps1` puts it.** Each one replaces and
  removes what the other installed. Installing also removes other registrations of the same layer,
  because OpenXR would otherwise find it twice.

## Windows' list of installed apps

While any part is installed, SimOptix is listed in Windows' Settings, under Apps, Installed apps. Its
**Uninstall** removes all three parts and the listing. It runs setup's own copy in
`C:\Program Files\simoptix`, which copies itself to the temporary folder to do the work, because a
running program can't delete its own file. Windows deletes that temporary copy at the next restart.

## Files

| File | What |
|---|---|
| `setup_main.cpp` | Starts setup: the window, or the removal with `/uninstall` |
| `setup_window.cpp` | The window and what its buttons do |
| `setup_look.cpp` | The window's layout, colours and fonts, and its drawing |
| `setup_status.cpp` | Which buttons each row shows, and whether a part can be changed now |
| `setup_probe.cpp` | Reads what is installed, the layer's registrations, OBS and LMU |
| `setup_install.cpp` | Installs and removes each part, and keeps the entry in Windows' installed apps |
| `setup_uninstall.cpp` | The removal of everything that Windows' installed apps start |
| `setup_places.cpp` | Where each part goes, and the registry keys |
| `setup_parts.h` | The three parts, their files and the resources that carry them |
| `setup_facts.h` | What setup found on the PC, as plain data |
| `setup_payload.cpp` | The files inside the exe: finding, comparing and writing them |
| `setup_signature.cpp` | A file's code signature, by Windows' own check |
| `setup_registry.cpp`, `setup_system.cpp` | The registry, folders, processes and OBS's version |
| `setup.rc` | Puts the two plugins and the layer into the exe |
| `setup.manifest` | Administrator rights, sharp text at any display scale, and Windows' current controls |
| `build.bat` | Builds both plugins and setup |

# The OpenXR layer: build, check, sign, install, and the Easy Anti-Cheat test

Terms: `reference\terms.md`. What the layer is and does: `vr_mirror\frame_sources\openxr\README.md`.
What each start of the game showed: `findings\openxr.md`.

## Build and check

```
vr_mirror\frame_sources\openxr\build.bat
```

```
"vr_mirror\frame_sources\openxr\bin\check\Le Mans Ultimate.exe"
```

The check program runs the freshly built layer the way the game will, before the layer goes anywhere
near the game.
- It loads a real OpenXR loader (SteamVR's, or the one `--loader` names), and switches the layer on for
  itself only.
- It behaves like LMU: the same name and extensions, a D3D11 session, one image buffer per eye in the
  game's format and usage, and five seconds of frames. It clears the left eye to a colour that encodes
  the frame's number.
- Meanwhile it reads what the layer publishes, as VR Mirror does, on a device of its own.
- It fails unless:
  - every frame record holds what it submitted
  - nearly every frame after the first copy was copied
  - every copy read back shows its own frame
  - the layer's clock reading matches the runtime's own conversion
- It carries the game's file name only because the layer wakes up for no other name. It touches no game
  file.
- It needs the headset on, with Pimax Play running. Without them it stops at `xrGetSystem` (−35, no
  headset available) and says so. The log then shows only the handshake and the instance.

Afterwards the layer's log (`%LOCALAPPDATA%\simoptix\openxr_frame_source.log`) should show the runtime,
the time conversion asked for, the session, both buffers, the frame layout, the frame source's lines,
and no errors. The copy time it logs is not the game's: an image cleared to one colour is stored
compressed, and copies much faster than a drawn one.

An installed copy of the layer wakes up in the check program too, because the program is called
`Le Mans Ultimate.exe`. So the check switches off an installed copy of the same name for itself
(`DISABLE_XR_APILAYER_SIMOPTIX_frame_source`).

## Signing

Easy Anti-Cheat keeps the layer out of LMU unsigned, and also signed with a certificate Windows
doesn't trust. Signed with a code signing certificate Windows trusts, the layer loaded in LMU with
Easy Anti-Cheat (`findings\openxr.md`). So every build that goes into the game is signed, and a
rebuilt DLL needs signing again.

`build.bat` signs the DLL when `SIMOPTIX_SIGN_THUMBPRINT` holds the SHA-1 thumbprint of a code
signing certificate in the user's certificate store. The maintainer's certificate is Certum's
open-source one, with its key in Certum's cloud (SimplySign).

1. Log in to SimplySign Desktop with the code from the SimplySign phone app. The certificate is in the
   user's store only while SimplySign Desktop is logged in, and the login lasts a limited time.
2. Find the certificate's thumbprint:

   ```
   Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject, Thumbprint, NotAfter
   ```

3. Set the variable for the user, so that `install.bat`, which starts from Explorer, sees it too.
   Windows hands it only to programs started afterwards.

   ```
   setx SIMOPTIX_SIGN_THUMBPRINT <thumbprint>
   ```

4. Build. After the check program, `build.bat` signs the DLL with SHA-256 and Certum's timestamp
   server, then checks the signature against the roots Windows trusts. It ends with "The layer is
   signed."

The build fails in these cases, so `install.bat` installs nothing:
- the certificate isn't in the store, for example with SimplySign Desktop logged out ("No
  certificates were found that met all the given criteria")
- the chain ends at a root Windows doesn't trust, as a self-signed certificate's does
- `signtool` isn't on the path that Visual Studio's environment sets up

Without the variable, the build says the layer is unsigned and succeeds.

- The timestamp keeps the signature valid after the certificate expires. OpenXR Toolkit's DLL, signed
  without one, is reported as not valid since its certificate expired.
- `install.ps1` shows the installed DLL's signature status. It reads "Valid" for a signed layer.
- To check a DLL by hand, run `signtool verify /pa /v <dll>` in a Visual Studio developer prompt.
- The check program is unsigned. It never goes into the game.

## Install for the game

Close LMU and double-click `vr_mirror\frame_sources\openxr\install.bat`.
1. It builds the layer first, so it never installs an old build. The build signs it when
   `SIMOPTIX_SIGN_THUMBPRINT` is set ("Signing", above), and stops if signing fails.
2. It asks Windows for administrator rights.
3. It runs `install.ps1` in a window that stays open, so the result can be read.

`install.ps1` copies the DLL and its manifest to `C:\Program Files\simoptix\openxr_frame_source\`, and
registers it in `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` (value 0 = on, 1 = off).
`uninstall.bat` and `uninstall.ps1` remove both, the same way.

By hand, in PowerShell as administrator, with the full path, because an administrator window starts in
`C:\Windows\system32`:

```
powershell -ExecutionPolicy Bypass -File "<repo>\vr_mirror\frame_sources\openxr\install.ps1"
```

## Starting LMU without Easy Anti-Cheat

Easy Anti-Cheat refuses the unsigned layer (`findings\openxr.md`). So an unsigned layer runs in LMU
started without it, the way the LMU quad-view mod's offline mode does. Online play is disabled in that game.

1. Start Steam and log in. Started without Steam, the game hands its start to Steam, which goes
   through Easy Anti-Cheat.
2. Run `Le Mans Ultimate.exe +XR` from the game's folder, with that folder as the working directory.

To compare with a start without the layer, set `DISABLE_XR_APILAYER_SIMOPTIX_frame_source=1` for that
start.

## The Easy Anti-Cheat test

Unsigned, or signed with a certificate Windows doesn't trust, the layer is kept out by Easy
Anti-Cheat. Signed with a trusted certificate, it loaded offline, and online play stayed enabled
(`findings\openxr.md`).

Test a signed DLL cheapest first, and stop at the first step where the game doesn't start:
1. **Offline.** Install it, start LMU the usual way, stay out of online sessions, and drive a few laps.
   The layer loads whenever LMU runs in OpenXR mode, however it is started. Write down whether the game
   starts, any Easy Anti-Cheat message, and whether online play is enabled.
2. **Online.** The same, in an online session. This risks the LMU account, so do it only after step 1
   went cleanly.

Where to look after each start:
- **VR Mirror's OBS log** (`%APPDATA%\obs-studio\logs`), with an OpenXR Mirror source shown: "the
  layer runs in the game", the layer's textures opened, and the frames per 10 s. With Easy Anti-Cheat
  the layer wrote no log of its own, so this is the evidence that it loaded.
- **Easy Anti-Cheat's launcher log**, in `%APPDATA%\EasyAntiCheat\` under the game's folder:
  "Easy Anti-Cheat successfully loaded in-game".
- **Windows' System log**, event 26: an "Untrusted system file" popup names a refused DLL.

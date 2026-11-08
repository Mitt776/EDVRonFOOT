EDVRonFOOT - native VR on foot for Elite Dangerous: Odyssey
============================================================

EDVRonFOOT is a fork of EDVR (characterecho-sean, MIT license,
https://github.com/characterecho-sean/edvr-unofficial-patch). It keeps EDVR's
native OpenXR VR and adds stereo VR on foot: head aiming, the on-foot panels
and menus in the headset, and a clean aim point.

Source, issues and new releases: https://github.com/Mitt776/EDVRonFOOT

WHAT YOU NEED
-------------

- A PC VR headset and an OpenXR runtime selected in Windows. The on-foot
  features are tested with Quest 3 over Link, with SteamVR and with Meta
  Quest Link as the OpenXR runtime; other runtimes may work but are not
  tested on foot. For a Quest over Link, Meta Quest Link directly is
  noticeably faster: see the user guide on the GitHub page.
- Elite Dangerous: Odyssey on the game build this release supports. The
  installer checks the build and refuses to install on one it does not know.

INSTALL
-------

1. Close Elite Dangerous.
2. Unzip this archive and run EDVRonFOOT-installer.exe.
   Windows SmartScreen may warn that the program is not signed: choose
   "More info" and "Run anyway".
3. Choose the game directory (the installer usually finds it) and Install.
   Your existing edvr.ini, if any, is kept.
   Leave "Keep the settings I have changed" checked: unchecked, the
   installer replaces edvr.ini with the defaults (your old file goes to
   edvr_backup\ in the game folder).
4. Start the game.

UNINSTALL
---------

Run EDVRonFOOT-installer.exe again and choose Uninstall. It removes the mod's
files and puts back the game's original files.

AFTER A GAME UPDATE
-------------------

A game patch changes the game build. Until an EDVRonFOOT update for the new
build is released, the mod may not start: run the installer and choose
Uninstall to play without it.

PROBLEMS
--------

Run the installer and press "Save logs": it writes one zip to your Desktop.
Attach it to an issue on the GitHub page above.

LICENSE
-------

MIT, see LICENSE.txt. Third-party notices: OPENXR-LOADER-LICENSE.txt,
DXBC-HASH-LICENSE.txt and, when included, NVIDIA-DLSS-LICENSE.txt and
FIDELITYFX-SDK-DX11-LICENSE.txt.

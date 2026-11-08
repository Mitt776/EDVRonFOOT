# EDVRonFOOT

**Native stereo VR on foot for Elite Dangerous: Odyssey.**

[Русская версия](README.ru.md) · [Download (Releases)](https://github.com/Mitt776/EDVRonFOOT/releases)

EDVRonFOOT is a fork of [EDVR](https://github.com/characterecho-sean/edvr-unofficial-patch) by characterecho-sean
(MIT). EDVR gives Elite Dangerous a native OpenXR VR path and many VR fixes in the ship. Out of the box the game
shows a flat "cinema" screen when you leave your seat; EDVRonFOOT keeps you in real stereo VR on foot.

## What it does on foot

- **Real stereo with head tracking** instead of the flat cinema screen: stations, settlements, planets.
- **Weapons and tools work**, with **head aiming**: the weapon follows your head, the sight lines up with your
  dominant eye when you aim.
- **The on-foot HUD in the headset** — health, oxygen, compass, the game's own sight and hints — on a curved sheet in
  front of you.
- **On-foot panels and menus in the headset**: inventory, the radial menu, mission board, bartender and traders,
  shops, the information panel, the summary, the main menu.
- **A clean aim point**: the stray dot the game leaves in the middle of the view is removed; weapon sights and tool
  markers stay.

Everything EDVR does in the ship stays as it is.

## What you need

- Windows 10/11 and a PC VR headset.
- An **OpenXR runtime** selected in Windows. Tested on foot with **SteamVR** as the OpenXR runtime (Quest 3). Other
  runtimes may work but are not tested on foot.
- **Elite Dangerous: Odyssey, game build `2026.10.23.334925`.** Horizons is not supported.

> **The mod is tied to one game build.** After a game patch the game build changes, and with the old mod installed
> the game will not start at all (no error window). The installer refuses to install on a build it does not know.
> After a patch, uninstall the mod (below) and wait for an EDVRonFOOT update.

## Install

1. Close Elite Dangerous.
2. Download `EDVRonFOOT-<version>.zip` from [Releases](https://github.com/Mitt776/EDVRonFOOT/releases), unzip it
   and run `EDVRonFOOT-installer.exe`.
   The installer is not signed, so Windows SmartScreen may warn: choose **More info → Run anyway**.
3. Pick the game directory (usually found automatically) and press **Install**. Your existing `edvr.ini`, if you
   have one, is kept.
   **Leave "Keep the settings I have changed" checked.** Unchecked, the installer replaces `edvr.ini` with the
   defaults (your old file goes to `edvr_backup\` in the game folder).
4. Start the game. In the headset, **F8** opens the EDVR menu.

## Uninstall

Run `EDVRonFOOT-installer.exe` and press **Uninstall**. It removes the mod's files and puts the game's original files
back.

## Problems

Run the installer and press **Save logs**: it writes one zip to your Desktop. Open an
[issue](https://github.com/Mitt776/EDVRonFOOT/issues) and attach it.

## For developers

The code is in [`edvr/`](edvr/). Build notes (in Russian): [`edvr/README_EDVRONFOOT.md`](edvr/README_EDVRONFOOT.md);
the original EDVR documentation: [`edvr/README.md`](edvr/README.md); where this copy comes from:
[`edvr/UPSTREAM.md`](edvr/UPSTREAM.md). Research notes are in [`docs/`](docs/), change proposals in
[`openspec/`](openspec/).

## License and thanks

MIT — see [`edvr/LICENSE`](edvr/LICENSE). Huge thanks to **characterecho-sean**, the author of
[EDVR](https://github.com/characterecho-sean/edvr-unofficial-patch): EDVRonFOOT is built on top of their work.
EDVRonFOOT is an unofficial fan project, not affiliated with Frontier Developments.

# EDVRonFOOT user guide

[Русская версия](GUIDE.ru.md) · [Back to the README](README.md)

How to use the mod in the game, which settings matter, and what each one changes. Install and uninstall are in the
[README](README.md#install).

## Where the settings are

All settings live in one file, **`edvr.ini`**, next to `EliteDangerous64.exe` in the game folder. You can change
them in three ways; all three write the same file:

1. **The in-headset menu: press F8 in the game.** The easiest way, with the headset on.
2. **The installer's Settings tab**: run `EDVRonFOOT-installer.exe` and open **Settings**. It shows the main
   settings, the on-foot ones under **Native VR on foot (EDVRonFOOT)**, with a search box.
3. **By hand**: open `edvr.ini` in Notepad. Every setting has a comment above it saying what it does.

Most settings take effect **within a second, while the game runs**. A few need a **game restart**; the menu marks
them, and the comment in `edvr.ini` says so.

Your `edvr.ini` survives updates of the mod. The installer keeps your values when it installs a new version, as long
as **"Keep the settings I have changed"** stays checked.

## The F8 menu

Press **F8** in the game: a settings panel appears where you are looking and stays there.

| Key | What it does |
|---|---|
| **Up / Down** | pick a row |
| **Left / Right** | change the value |
| **Enter** | switch on/off |
| **Tab** | next page |
| **Escape** or **F8** | close the menu |
| **Shift + F8** | move the panel to where you are looking now |

The keys you use for Elite's own cockpit panels work in the menu too. While the menu is open the game does not see
the keyboard; your HOTAS, gamepad and mouse still fly the ship. Rest on a row for a moment and a card beside it
explains the setting.

Pages: **Fixes** (ship, space, intro), **Performance** (anti-aliasing, sharpness, resolution, frame-rate readout),
**Explorer Cam**, **On foot** (the settings of EDVRonFOOT, see below), **Hotkeys**, **Monitor** (frame rate and
frame times) and **Status**.

### Developer mode

**F8 → Tab to the Performance page → Developer mode → on** shows three more pages: **Advanced**, **Experimental**
and **Instruments**. You do not need them for the on-foot settings. On the Advanced and Instruments pages, change
something only when the log or a developer asks you to.

## On foot (EDVRonFOOT)

These are the mod's own settings: **F8 → On foot page**, or the installer's Settings tab, section **Native VR on
foot (EDVRonFOOT)**. The defaults are what we fly with; you normally do not need to change anything. All of them are
live: they change the next second.

| In the menu (`edvr.ini` key) | Values | What it does |
|---|---|---|
| **Native VR on foot** (`native_vr_on_foot`) | **1** / 0 | 1: on foot you see the world in real stereo with head tracking. 0: the game's own flat "cinema" screen on foot. Best switched in the ship. |
| **On-foot interface** (`native_vr_on_foot_hud`) | **body** / visor / off | Where the on-foot HUD (health, oxygen, compass, sight, hints, dialogues) is shown: on a curved sheet 1.4 m away. **body**: the sheet stands in front of your body, your head turns past it, the stick turns it with you. **visor**: the sheet follows your eyes like a helmet visor. **off**: no HUD. |
| **On-foot aiming eye** (`native_vr_on_foot_aim_eye`) | **right** / left / off | When you hold **LT** (aim) with a weapon, the view shifts by about 3 cm so this eye looks straight down the sights. Pick your dominant eye. off: LT does nothing extra. |
| **On-foot head aim** (`native_vr_on_foot_head_aim`) | **on** / off | on: the weapon and the aim point follow where your head looks. off: the weapon stays with your body, as in the game. Head aim steps aside by itself when a menu, map, panel or the lift is open. |
| **On-foot aim dot** (`native_vr_on_foot_aim_dot`) | **hide** / show | hide: the plain dot the game keeps in the middle of your view without a weapon is removed. Weapon sights, tool markers, door markers and hints stay. show: the game's dot as it is. |
| **On-foot scope zoom** (`native_vr_on_foot_scope_zoom`) | **on** / off | on: when you aim the Manticore Executioner with LT, its scope glass in your aiming eye is magnified as much as the game zooms on a flat screen (about 1.7x, about 4x with the Scope mod), once the scope is at your eye. The frame, the world around it, the other eye and the HUD stay as they are; other weapons are not magnified. off: no magnifier. |
| **Weapon stability** (`weapon_stability`, Fixes page) | **1** / 0 | Keeps weapons and tools steady while you walk and turn on foot. Costs about one frame of delay on foot. |

**Bold** is the default.

## Picture quality and performance

These are on the **Performance** page of the F8 menu; all but the frame-rate readout are also in the installer's
Settings tab.

| In the menu (`edvr.ini` key) | Values | What it does |
|---|---|---|
| **Anti-aliasing** (`temporal_aa`) | **off** / dlss | **dlss**: NVIDIA DLSS (needs an RTX card): calm edges without shimmer. If Elite's **HMD Quality** is 1.0 or more, this works as DLAA (best picture). Below 1.0 the game renders smaller and DLSS brings it back to size, which buys frame rate. Set Elite's own anti-aliasing to Off or SMAA with this on. **Needs a game restart.** |
| **DLSS preset** (`temporal_aa_model`) | **K** / J / L / M / Automatic | NVIDIA's model. K is the default; the others are for comparison. |
| **Sharpening** (`render_sharpness`) | **0** … 1 | Gives back some edge contrast that DLSS softens. 0.3 is a fair start with DLSS on. Too much makes edges ring and fine lines shimmer. Does nothing with anti-aliasing off. |
| **UI quality** (`ui_quality`) | off / **100** / 125 | Cockpit panels and menus are drawn as sharp as at HMD Quality 1.0 (100) or 1.25 (125), even when the world renders smaller. The menus part needs anti-aliasing on. 125 costs more video memory. |
| **OpenXR resolution** (`openxr_resolution`) | empty = 100% | Render width per eye for the headset you wear. Set it in the menu with the headset on; the menu shows the size and the percent of what your runtime recommends. **Needs a game restart.** |
| **Frame-rate readout** (`fps_overlay`, menu section) | **off** / on | A small readout pinned to your view: frames per second, GPU time, dropped frames. **Readout position across / up** move it. **FPS readout in menu** keeps it visible while the menu is open. |

**Where to start for frame rate:**
1. Elite's own **HMD Quality** (game options) is the biggest lever.
2. With an RTX card, set Anti-aliasing to **dlss** and HMD Quality below 1.0 (for example 0.75). DLSS brings the
   picture back to size.
3. Turn on the **Frame-rate readout** and compare.

### Quest over Link: the fastest setup

Measured in a settlement conflict zone (Quest 3 over Link, 72 Hz, RTX 4080 SUPER): **18 fps** with SteamVR as the
OpenXR runtime and ASW on, **22–24** with ASW off, **26–31** with Meta Quest Link directly and ASW off. Lowering the
resolution did not help at all: in a big fight the game itself (the CPU) is the limit, and SteamVR in between and ASW
only add waiting on top of it.

1. **Meta Quest Link as the OpenXR runtime**: in the Meta Quest Link app on the PC, **Settings → General → OpenXR
   Runtime → Set as active**. Launch the game from Steam as usual; SteamVR is not needed. (Back to SteamVR: SteamVR
   settings → OpenXR → set SteamVR as the OpenXR runtime.) Do not use the game's own Oculus mode: EDVRonFOOT does not
   support it.
2. **ASW off**: `OculusDebugTool.exe` (in `...\Oculus\Support\oculus-diagnostics\`) → **Asynchronous Spacewarp →
   Disabled**. When the game cannot hold 36 fps, ASW drops it straight to 18. ASW goes back to Auto when Link
   restarts, so check it before you fly. Without ASW head turns are a little less smooth when the frame rate is low;
   try both and keep what feels better.

## Ship and space

On the **Fixes** page. The defaults fix things that look wrong in a headset; **stock** or **off** gives you the
game's own behaviour back.

| In the menu (`edvr.ini` key) | Values | What it does |
|---|---|---|
| **Both eyes share one brightness** (`share_exposure`) | **1** / 0 | Near a star one eye can go darker than the other; this keeps them equal. Needs a restart; **Scroll Lock** toggles it during a session. |
| **FSS eye sync** (`fss_eye_sync`) | **on** / off | The Full System Scanner shows the same picture in both eyes. |
| **Night vision pulse stability** (`night_vision_stability`) | **1** / 0 | The night-vision pulse line stays at a stable distance when you turn your head. |
| **Stop the one-frame flash on jumps** (`transition_flash`) | **1** / 0 | Hides the one wrong frame when you jump, drop from supercruise or close the galaxy map. Needs a restart. |
| **Sun glare** (`sun_glare`) | **vivid** / realistic / stock | The star's glare no longer rides your head. vivid: all of it, locked to the world. realistic: only the glow a real eye would see. |
| **Smoke and steam plumes** (`particle_billboard`) | **steady** / stock | Smoke and geyser plumes stay upright when you tilt your head. |
| **Hyperspace tunnel starfield** (`witchspace_stars`) | **on** / off | off: the jump tunnel without stars streaking past. |
| **Supercruise space dust** (`supercruise_dust`) | **on** / off | off: no dust streaks in supercruise (can help Virtual Desktop's Space Warp). |
| **Normal-space dust** (`normal_space_dust`) | **on** / off | off: no dust specks near planets and stations. |
| **RemLok helmet lines** (`remlok_lines`) | **outer** / hide / stock | The emergency helmet's edge lines at your temples, as a real helmet, instead of along your nose. **RemLok line angle** (`remlok_line_angle`, **46**) places them. |
| **High-wake marker flashing** (`wake_pulse`) | **off** / stock | off: removes the flashing marker under the speed readout when a high wake is selected. |

## Intro, loading and the flat screen

| In the menu (`edvr.ini` key) | Values | What it does |
|---|---|---|
| **Intro movie** (`intro_video`) | **screen** / stock / skip | screen: the launch movie on a big virtual screen instead of a small rectangle stuck to your face. skip: no movie at all. |
| **Intro background picture** (`intro_backdrop`) | **splash** / stock | A smoother backdrop behind the intro and the main menu, without banding. |
| **Loading screen dimming** (`loading_dim`) | **screen** / stock | Loading dialogs no longer dim your whole view. |
| **Loading screen hologram** (`holo_pattern`) | **steady** / stock | The ship hologram on the loading screen stops shimmering with your head. |
| **On-foot screen distance** (`panel_distance`) | **1.0** (0.5 … 2.0) | How far the game's flat screen sits: the "cinema" on foot (with Native VR on foot = 0) and the intro. |
| **Screen curve** (`panel_curvature`) | **0** … 1 | Bends that screen toward you. 0.3 with distance 0.7 is a comfortable start. |
| **Black surround for the on-foot screen** (`black_void`) | **1** / 0 | Pure black around the flat screen instead of grey. |
| **On-foot screen resolution: width** (`vscreen_res_width`) | **auto** / a width | How sharp that flat screen is. auto sizes it itself. Needs a restart. |

With Native VR on foot on, the flat screen is not used on foot, so the last four mostly matter for the intro.

## Night vision (experimental)

On the **Experimental** page (Developer mode).

| In the menu (`edvr.ini` key) | Values | What it does |
|---|---|---|
| **Realistic nightvision** (`night_vision_realistic`) | **0** / 1 | Outlines instead of a green fill; the terrain keeps its colour. |
| **Night vision brightness** (`night_vision_brightness`) | **8.0** (1 … 16) | How bright the terrain is with Realistic nightvision on. |

## Hotkeys

On the **Hotkeys** page: pick a row, press Enter, press the key you want (a keyboard key, a gamepad button or a HOTAS
button). Escape cancels, Delete clears. The game still sees these keys; the page warns when Elite uses the same one.

| Key | Default | What it does |
|---|---|---|
| **Menu key** (`menu`) | **F8** | Opens and closes the menu. Avoid F10 (game screenshots) and F12 (Steam). |
| **Explorer Cam key** (`explorer_cam`) | **F5** | Explorer Cam: on foot, the game's free camera at your commander's eyes. Native VR on foot makes it mostly unnecessary. Empty turns it off. |
| **Brightness fix key** (`toggle_exposure`) | **Scroll Lock** | Toggles "Both eyes share one brightness" to compare. |
| **Camera history key** (`dump_camera`) | **Pause** | Writes the last ten seconds of the view's movement to the log. Press it right after you see a flash that got through, then send the logs. |

## If something goes wrong

- **Back to defaults for one setting:** in the menu, or delete its line from `edvr.ini`; a missing line means the
  default.
- **Back to all defaults:** close the game, rename `edvr.ini` (for example to `edvr-old.ini`) and start the game. Or
  run the installer with **"Keep the settings I have changed"** unchecked; your old file goes to `edvr_backup\` in
  the game folder.
- **A problem to report:** quit the game, run the installer, press **Save logs** and attach the zip to an
  [issue](https://github.com/Mitt776/EDVRonFOOT/issues).

## Everything else

`edvr.ini` has more settings than this guide: developer tools, safety switches and fine tuning. Each one is
described in the comment above it in the file itself; the original EDVR documentation is in
[`edvr/README.md`](edvr/README.md). Change those only when the log or a developer names one.

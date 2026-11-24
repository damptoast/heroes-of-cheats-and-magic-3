# Heroes of Cheats and Magic 3

A gamepad overlay and cheat panel for [VCMI](https://vcmi.eu/) 1.7.5 running Heroes of Might and
Magic III Complete. It is a small Windows program that runs next to VCMI, plus a settings-only VCMI
mod. Neither VCMI nor the original game files are modified: the installer only changes VCMI's own
settings and key bindings, and it backs up everything it touches so the uninstaller can put it all
back.

What you get:

- **Full gamepad play.** The left stick drives the mouse, the right stick walks the hero, and every
  button has exactly one job per screen. LT and RT work as modifiers for a second and third layer.
- **A cheat panel** drawn with the game's own dialog texture and fonts: quick cheats, resources,
  levels, experience, army, secondary skills and artifacts, with lists built from the base game and
  every mod you have enabled.
- **A small VCMI mod** that turns on the tavern "invite hero" picker and unlocks all campaigns.
- **A bug reporter** that writes a report with your paths scrubbed and opens a filled-in GitHub
  issue.

Nothing from Heroes III ships with this. The panel art, fonts and name tables are read from your own
copy of the game when you install.

## Requirements

- Windows 10 or 11 (PowerShell 5.1 is built in, nothing else to install)
- VCMI 1.7.5. Other 1.7.x versions will probably work, but the key binding patch was checked
  against 1.7.5.
- Your own Heroes III data (the GOG "Complete" edition works) set up in VCMI, and VCMI started at
  least once so it has written its config files
- An XInput (Xbox-style) controller. A PlayStation pad works through Steam Input or DS4Windows.
- VCMI in windowed or borderless fullscreen mode. The panel is a normal window drawn on top of the
  game, so exclusive fullscreen will hide it.
- Cheats allowed in the game you play. "Allow cheats" is on by default for new games.

## Features

### Gamepad

The overlay reads the whole controller itself and sends VCMI spare keys that nobody types (F13 to
F24, a few keypad keys, Insert, Scroll Lock). The installer adds those keys to VCMI's keyboard
bindings, and on each screen VCMI uses whichever action makes sense: the same key is "continue
route" on the map, "wait" in battle and "OK" in a dialog. The lists in `vcmi-pad-keys.json` were
checked so that no key does two things on the same screen.

VCMI's own controller support is switched off during install. It draws a second, software cursor
and has no chords, so the two would fight.

The pad only acts when VCMI or the panel is the active window. Alt-tab to something else and the
overlay leaves your pad alone.

### Pad mouse

The left stick moves the real Windows cursor. Speed follows the tilt on a curve: a nudge creeps a
pixel or two for exact clicks, full tilt crosses the screen in about a second. A is left click, X is
right click (hold X to inspect, as with the right mouse button in Heroes).

Tap L3 to switch between smooth mode and grid mode. Grid mode moves the cursor in fixed steps
(8 to 80 px, set in the panel), which is handy for menus and the town screen. Over the panel, grid
steps jump from control to control instead. A notice at the top of the game shows which mode you
are in.

There is code for snapping grid steps to the centres of adventure map tiles, but it is switched off
for now (see Known issues).

### Cheat panel

Open it with F2, by holding L3, or with RT + Start. The panel sits on top of the game window, can be
dragged by its header, and collapses with the `[-]` button or a double-click on the header. It
follows the game window when you move that. Only one section is open at a time:

| Section | What it does |
|---|---|
| Quick cheats | Reveal map, hide map, full movement, all spells, war machines, build town, all artifacts, all scrolls, max luck, max morale, god mode, win |
| Resources & experience | Resources (n of each, gold n x 1000), level up, experience. Each has `-`/`+` presets and a box you can type a number into. |
| Army | Pick faction, level and unit, then a count. VCMI puts that many in every **empty** slot of the selected hero. |
| Secondary skills | Pick a skill and Basic / Advanced / Expert, then Give skill |
| Artifacts | Pick a class, page through the list, Give artifact. It goes to the first free slot, else the backpack. |
| Settings | Mouse mode, mouse speed, grid step, Report a bug, Rescan mods, Exit overlay |

The panel works by typing VCMI's own console cheats (`vcmieagles`, `vcmiarmy`, `vcmiskill` and so
on) into the game for you. Afterwards it reads VCMI's log to see whether the cheat did anything, and
the status line at the bottom of the panel says so. If VCMI accepted the cheat but nothing changed,
it tells you what to check. Usually that means no hero is selected, or the hero has no empty army
slot.

Hero and town cheats apply to whatever hero or town is selected on the adventure map. Cheats only
work on the adventure map, so close hero and town screens first.

The army, skill and artifact lists come from `H3Overlay_catalog.txt`, which `build_catalog.ps1`
builds from the base game plus every enabled mod. Each entry keeps the full VCMI identifier
(`core:pikeman`, `hota.cove:corsair`), so two mods with a same-named creature can't make a cheat
silently miss. If you turn mods on or off in the launcher, the overlay notices when the game starts
and rebuilds the list in the background. You can also press Rescan mods.

### VCMI mod

`vcmi-mod/h3-overlay` is a settings-only mod (no content, no art). It sets two VCMI game options:

- `heroes.tavernInvite`: every tavern gets an "invite hero" picker listing every hero not already
  in play.
- `campaign.unlockAll`: every campaign can be started without finishing the ones before it.
  Campaign progress itself is not changed.

The installer enables it in your active mod preset. You can switch it off in VCMI's Mods tab
without affecting the overlay.

### Bug reporter

RT + Back, or Report a bug in the panel's Settings section. See "Reporting bugs" below.

## Controls

LT and RT are modifiers. On their own they do nothing, and a button pressed while one is held fires
only its chord, even if you let go of the trigger first. Holding both triggers does nothing.

### Pad, in the game

| Button | Adventure map | Battle | Other screens |
|---|---|---|---|
| Left stick | mouse | mouse | mouse |
| Right stick | walk the selected hero one tile (8 directions) | - | move through lists, sliders, previous/next town |
| A | left click | left click | left click |
| X | right click (hold to inspect) | right click | right click |
| B | back / cancel, leave world view | back | close, back |
| Y | continue the hero's route | wait (tactics phase: end tactics) | OK |
| D-pad up / down | first hero / next hero | - | up / down in lists; recruitment: max / min |
| D-pad left / right | first town / next town | - | left / right |
| LB | open selected hero or town | defend | town screen: quick recruitment |
| RB | cast spell | cast spell (tactics: next unit) | - |
| Back | end turn | auto-combat | - |
| Start | game menu | game menu | game menu |
| L3 tap | smooth / grid mouse | same | same |
| L3 hold | open / close the cheat panel | same | same |
| R3 | surface / underground | show hero stats | - |

| LT + | Adventure map | Battle |
|---|---|---|
| A | visit object | creature spell |
| B | sleep / wake hero | retreat |
| X | dig for the grail | surrender |
| LB / RB | zoom out / zoom in | - |
| D-pad up | view world | active unit info |
| D-pad down | quest log | toggle turn queue |
| D-pad left | scenario info | quick spells |
| D-pad right | marketplace | - |
| Start | quicksave | - |
| Back | quickload | - |

| RT + | Adventure map | Anywhere in the game |
|---|---|---|
| A | kingdom overview | - |
| D-pad up | statistics | - |
| D-pad down | puzzle map | - |
| D-pad left | thieves guild | - |
| Start | - | open / close the cheat panel |
| Back | - | report a bug |

Buying out a whole town: LB (open the town), LB (quick recruitment), D-pad up (max all), Y (buy).

### Pad, on the cheat panel

| Button | Action |
|---|---|
| D-pad or right stick | jump to the next control in that direction |
| A | press the control under the cursor |
| LB / RB | step the list or amount under the cursor back / forward |
| LT / RT | bigger step: 10 entries in the skill and artifact lists, 3 presets for amounts |
| B | close the panel and go back to the game |
| Left stick | still moves the mouse |

### Keyboard and mouse

| Key | Action |
|---|---|
| F2 | open / close the cheat panel (when VCMI or the panel is in front) |
| Esc | close the panel (when the panel is in front) |
| Mouse wheel on a control | step its list or amount |
| Click a number box, then type digits | set an amount; Backspace deletes, Enter fires the cheat |
| Double-click the panel header | collapse / expand |
| Alt+F4 on the panel | hide it (the overlay keeps running) |

The pad keys are ordinary VCMI key bindings, so all of VCMI's normal keyboard shortcuts keep
working.

## Install

1. Download the release zip from the Releases page and unzip it somewhere outside the VCMI folder,
   for example your Downloads folder.
2. Make sure VCMI has been started at least once and is closed.
3. Double-click `install.cmd`.
4. It searches for VCMI and your Heroes III data: the registry (VCMI and GOG install entries),
   VCMI's own data folders, and common install places on every fixed drive. If it can't find one of
   them, a folder picker opens. Point it at the VCMI folder (the one with `VCMI_launcher.exe`) or
   at the Heroes III folder (the one with `Data\H3bitmap.lod`).
5. It then:
   - copies `H3Overlay.exe` into the VCMI folder,
   - extracts the panel art and fonts from your game's LOD files into `<VCMI folder>\H3Overlay`
     and builds the creature/skill/artifact catalog,
   - installs the `H3 Overlay` VCMI mod and enables it in your active preset,
   - adds the pad keys to VCMI's key bindings (both the install folder copy and, if you have one,
     the copy in your user folder) and switches VCMI's controller support off,
   - puts a **VCMI Heroes 3 (H3 Overlay)** shortcut on your desktop.
6. Press a key to close the window when it says Done.

From then on, start the game with the desktop shortcut. It starts the overlay, which opens the VCMI
launcher if it isn't already running. The overlay quits by itself when VCMI closes.

Every file the installer changes is listed in `<VCMI folder>\H3Overlay\install-manifest.json`, with
the original saved in `<VCMI folder>\H3Overlay\backup`.

If the catalog step fails (the warning says so), the overlay still works but the Army, Skills and
Artifacts lists will be empty. Start VCMI once and press Rescan mods in the panel.

### Manual install

If you'd rather not run the installer:

1. Copy `H3Overlay.exe` into your VCMI folder.
2. Extract the art from your game:
   ```powershell
   powershell -ExecutionPolicy Bypass -File extract_assets.ps1 -Data "<Heroes III folder>\Data" -Out "<VCMI folder>\H3Overlay"
   ```
3. Copy `build_catalog.ps1` into `<VCMI folder>\H3Overlay` and build the catalog:
   ```powershell
   powershell -ExecutionPolicy Bypass -File "<VCMI folder>\H3Overlay\build_catalog.ps1" -Vcmi "<VCMI folder>" -Assets "<VCMI folder>\H3Overlay"
   ```
4. Copy `vcmi-mod\h3-overlay` into `Documents\My Games\vcmi\Mods\` and enable it in VCMI's Mods tab.
5. In `Documents\My Games\vcmi\config\settings.json`, set `"input": { "enableController": false }`.
6. In `<VCMI folder>\config\keyBindingsConfig.json`, add each key from `vcmi-pad-keys.json` to every
   shortcut listed under it. For example, F13 goes into `globalCancel`, `globalReturn`,
   `adventureExitWorldView` and `mainMenuBack`. Empty the `joystickAxes` and `joystickButtons`
   blocks. Do the same in `Documents\My Games\vcmi\config\keyBindingsConfig.json` if that file
   exists, because it overrides the install folder copy.
7. Start `H3Overlay.exe` from the VCMI folder.

Make your own backups first. The uninstaller only knows about changes the installer made.

## Uninstall

Close VCMI, then double-click `uninstall.cmd`. It finds VCMI the same way the installer does, then:

- restores VCMI's settings, key bindings and mod list from the backups,
- removes `H3Overlay.exe`, the `H3Overlay` folder (art, backups, reports), the catalog, the VCMI mod
  and the desktop shortcut,
- deletes `H3Overlay.ini` and `H3Overlay.log`.

If VCMI rewrote one of those files after you installed (an update, or you saved new hotkeys),
running `install.cmd` again takes that newer file as the one to restore later.

## Updating

Close VCMI, unzip the new release, and run its `install.cmd`. You don't need to uninstall first. The
installer replaces the exe, rebuilds the art and catalog, and patches the key bindings again from
the original backup, so keys are never added twice. Your original backups are kept, so a later
uninstall still restores your pre-overlay settings.

After a VCMI update, run `install.cmd` again: a new VCMI version usually ships a fresh
`keyBindingsConfig.json` without the pad keys.

## Game assets

This repository and the release zip contain no Heroes III art, fonts or text. `extract_assets.ps1`
reads these straight out of your own `H3bitmap.lod` (and `H3ab_bmp.lod` if present) during install:

- `DIBOXBCK.PCX`, the dialog background, converted to `diboxbck.bmp`
- `BIGFONT.FNT`, `MEDFONT.FNT` and `SMALFONT.FNT`, the bitmap fonts
- `CRTRAITS.TXT`, `SSTRAITS.TXT` and `ARTRAITS.TXT`, the creature, skill and artifact names for the
  catalog

They end up in `<VCMI folder>\H3Overlay` on your machine and nowhere else. The `.gitignore` keeps
`.lod`, `.pcx`, `.fnt` and `.bmp` files out of the repository.

## Building from source

You need [Zig 0.14](https://ziglang.org/download/), which is used as the C compiler. Put it on
PATH, point `$env:ZIG` at `zig.exe`, or unpack it into `.tools\` in this folder (that folder is
git-ignored).

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Test
```

```powershell
powershell -ExecutionPolicy Bypass -File make-release.ps1
```

The first builds `dist\H3Overlay.exe`, the second builds and then runs the tests, and the third
builds `dist\H3Overlay-<version>.zip` with exactly what a player needs.

The build uses relative paths only, strips symbols and debug info and writes no timestamp, so the
same source always gives the same bytes. You can check a release by building it yourself and
comparing hashes.

`tests\run-tests.ps1` covers:

- path scrubbing, the log ring buffer and issue URL trimming in the bug reporter (`tests\test_report.c`)
- the installer, run against a fake VCMI and game folder in a temp directory: finding folders,
  backups, a second install changing nothing, VCMI rewriting its bindings between installs, and a
  clean uninstall (`tests\test-installer.ps1`)
- a scan of the built exe for user names and local paths (`tests\scan-binary.ps1`)

If you fork this, the bug reporter's repository URL is `H3_REPO_URL` in `H3_ReportCore.h`. You can
also override it with `-DH3_REPO_URL=\"...\"`.

### Layout

| Path | What |
|---|---|
| `H3Overlay.c` | the overlay: panel, cheats, pad input, pad-to-key mapping |
| `H3_Grid.c` | finds the map tile grid on a screenshot (used by grid snapping) |
| `H3_Report.c`, `H3_ReportCore.h` | bug report: collect, scrub, build the issue link |
| `vcmi-pad-keys.json` | the spare keys the pad sends and the VCMI shortcuts each one is added to |
| `vcmi-mod/h3-overlay` | the settings-only VCMI mod |
| `install.*`, `uninstall.*`, `lib/` | installer, uninstaller, folder finders, backup bookkeeping |
| `extract_assets.ps1`, `build_catalog.ps1` | read art and names from your game, build the catalog |
| `tests/` | tests |

## Known issues

- Grid mode moves in plain pixel steps. Snapping to adventure map tiles is in the code but switched
  off, because on real maps the tile finder sometimes locks onto 2 to 4 tiles instead of one.
  Turning grid mode on still writes `<VCMI folder>\H3Overlay\grid-debug.png` and logs the fit.
- Cheats are typed into VCMI's console, so they only work on the adventure map and need "Allow
  cheats" on for that game. With a hero or town screen open, VCMI ignores them and the panel says
  so.
- The army cheat fills **every empty** slot of the hero with the chosen unit (that is how VCMI's
  `vcmiarmy` works). A hero with a full army gets nothing.
- Exclusive fullscreen hides the panel. Use windowed or borderless.
- Only the first XInput controller (slot 0) is read.
- The key binding patch was checked against VCMI 1.7.5. If a later VCMI renames a shortcut, the
  installer stops with "Shortcut '...' not found" instead of guessing.

## Reporting bugs

In the game, hold RT and tap Back, or press Report a bug in the panel's Settings section. The
overlay then:

1. writes a report to `<VCMI folder>\H3Overlay\reports\report-<time>.txt`,
2. copies it to the clipboard,
3. opens a new issue on
   [github.com/damptoast/heroes-of-cheats-and-magic-3](https://github.com/damptoast/heroes-of-cheats-and-magic-3/issues)
   in your browser with the report filled in.

The report has the overlay and VCMI versions, whether a pad is connected, the mouse settings, the
overlay's recent messages and the end of VCMI's client log. Your VCMI folder, VCMI user folder and
home folder are replaced with `<game>`, `<vcmi-user>` and `<home>`, and any other `X:\Users\name`
path has the name replaced with `<user>`. Read it before you submit anyway. If the report was too
long for the link, the form says so: paste the clipboard over the Report box.

You can also open an issue by hand from the Issues tab. Attach `H3Overlay.log` from the VCMI folder
and say which controller you use.

## Credits and licence

- [VCMI](https://vcmi.eu/) ([source](https://github.com/vcmi/vcmi)), the open source Heroes III
  engine this runs on top of. No VCMI code is included here. The overlay only uses VCMI's console
  cheats, key bindings and mod settings.
- Heroes of Might and Magic III and its art, fonts and text belong to their owners and are never
  shipped here. Heroes of Might and Magic is a trademark of Ubisoft. This project is not
  affiliated with Ubisoft or the VCMI team.

The code here is MIT, see [LICENSE](LICENSE).

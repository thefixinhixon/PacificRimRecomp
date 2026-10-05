# House Launcher

Bespoke Qt6 Widgets launcher for our ReXGlue recomp games.
One launcher, one compiled-in profile per game (The Maw first).
Plumbing only for now: game detection, settings, safe launch.

## Layout

- src/gameprofile.h  - GameProfile struct + compiled-in profiles
- src/gamesettings.* - launcher settings (QSettings IniFormat)
- src/gamedetect.*   - game folder search + stored-root validation
- src/launcher.*     - arg assembly, /dev/shm vacuum, QProcess spawn
- src/mainwindow.*   - Game tab (status + PLAY) and Settings tab

## Build (pinned toolchain, recomp-dev distrobox)

    distrobox enter recomp-dev -- cmake -S ~/dev/projects/house-launcher -B ~/dev/projects/house-launcher/build -DCMAKE_BUILD_TYPE=Release
    distrobox enter recomp-dev -- cmake --build ~/dev/projects/house-launcher/build

## Roadmap

- STFS/XBLA extraction module (src/stfs/): let the launcher extract a
  users XBLA package into the game folder, like XBLA-Extract does.
- Theming (src/theme/): per-game art/accent instead of placeholder visuals.
- More compiled-in game profiles (Splosion Man, Dante, Condemned 2).
- Launcher-side pre-flight checks (disk space, stale shader cache).

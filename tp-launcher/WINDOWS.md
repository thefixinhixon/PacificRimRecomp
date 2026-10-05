# TP House Launcher — Windows port plan (Phase 2)

Phase 1 (done) moved every OS-specific behavior of the launcher behind
`src/platform.h` / `src/platform.cpp`:

- `platform::gameRootSearchBases()` — where Games libraries live
- `platform::programSetFiles(profile)` — the matched program-file set
- `platform::vacuumGuestMemory()` — pre-launch arena cleanup (no-op on
  Windows by design: the guest-memory backing there is a named
  pagefile mapping owned by the game process, destroyed by the kernel
  at exit; it cannot leak like a `/dev/shm` tmpfs file)

The Windows branches are written in pure Qt behind `#ifdef Q_OS_WIN`
but have never been compiled — there is no Windows kit on the dev
machine. Linux behavior is unchanged (verified: assembled launch
command lines byte-identical pre/post refactor for both games).

## Prerequisites

- Windows 10/11 x86_64
- Visual Studio 2022 (MSVC, C++20) — or clang-cl with the MSVC STL
- Qt 6 kit for MSVC (the launcher sticks to Qt 6.4-era APIs; 6.4+ fine).
  Qt Widgets only.
- CMake 3.16+

## Launcher build

Same CMake project, nothing special:

```
cmake -S . -B build-win-maw -DTP_GAME=maw
cmake --build build-win-maw --config Release
cmake -S . -B build-win-splosion -DTP_GAME=splosionman
cmake --build build-win-splosion --config Release
```

Produces `TPMawLauncher.exe` / `TPSplosionmanLauncher.exe`. The
QSettings ini scheme carries over unchanged (IniFormat, org
"HouseRecomp", per-game app name → `%APPDATA%`-side user scope).

## Game-side build

The games are ReXGlue recomp projects; the SDK and each game project
configure for Windows with MSVC the same way they do for Linux:

- 'Splosion Man: the SplosionManRecomp repo (config + manifest +
  committed `generated/` tree; the XEX stays out of git — the builder
  supplies their own `assets/default.xex`).
- The Maw: the maw project tree (same shape).
- **Set `REXGLUE_ENABLE_FIDELITYFX=ON` in the SDK configure** — with
  it off, `--present_effect=fsr` silently falls back to bilinear.
  This exact trap shipped once on Linux; audit the produced runtime
  for the `present_fsr` symbols (`FLAGS_present_fsr_quality_mode`)
  before packaging anything, the same strings audit used on Linux
  (plus the PipeWire audit's Windows equivalent: check the runtime's
  audio backend imports are the expected Windows ones).

## TODO confirmations in platform.cpp (do these first)

`platform::programSetFiles()` Windows branch currently returns
`{<exeName>.exe, librexruntime.dll, librexgpu-xenos.dll}` — the DLL
names are **guesses** carried over from the Linux `.so` names. Against
the first real Windows SDK build output:

1. Confirm the actual runtime / GPU-plugin DLL file names and the game
   exe name.
2. Fix that one list in `platform.cpp` (every consumer — exe-set
   resolution, error text — goes through it).
3. Confirm the exe-set layout the SDK produces (exe + DLLs in one
   folder is assumed by `resolveExecutableSet`, same as Linux).

Also confirm the Windows runtime's guest-memory backing really is a
process-owned mapping (the `vacuumGuestMemory()` no-op rationale); if
it writes real files somewhere, that's the one other platform.cpp
spot to revisit.

## GitHub Actions sketch (windows-latest)

One workflow, matrix over the two games:

1. `windows-latest` runner (MSVC preinstalled).
2. Install Qt: `jurplel/install-qt-action` (arch `win64_msvc2022_64`,
   modules: qtbase only — Widgets is in qtbase).
3. Game build: configure the game repo with the SDK,
   `-DREXGLUE_ENABLE_FIDELITYFX=ON`, Release; collect exe + runtime
   DLLs; run the strings audit for `present_fsr`.
4. Launcher build: configure this project with `-DTP_GAME=<game>`,
   Release; `windeployqt` the launcher exe.
5. Artifact = **folder ZIP**: launcher exe + its Qt runtime files +
   game exe + game runtime DLLs side by side (the layout
   `resolveExecutableSet` rule (2) expects — program set in the
   launcher's own folder), plus a README. **No game data** — users
   import their own XBLA package on first run, same as Linux.

## Tester-loop verification model

There is no Windows machine in the dev loop, so verification rides on
a human tester with a real Windows box (the model that worked for the
Linux ports, with the tester as the remote pilot):

1. Tester unzips the folder build, runs the launcher.
2. First run: detection finds their Games folder (or not) → Import
   XBLA package → PLAY.
3. Evidence comes back as files, not impressions: `last-command.txt`
   and the game log (both written into the data root's logs folder by
   the launcher/runtime), plus screenshots for anything visual.
4. Settings spot-checks are observable the same way as on Linux:
   change one knob, relaunch, diff `last-command.txt`.
5. Audio/input verdicts are the tester's (their session owns the
   devices — the Linux uid-session lesson applies in spirit).

Known Windows unknowns to burn down in the loop, in order: DLL names
(above), FidelityFX actually engaging (A/B at 720p + FSR vs bilinear),
audio backend behavior, controller behavior, and where the runtime
puts saves by default (so the launcher's data-root defaults match).

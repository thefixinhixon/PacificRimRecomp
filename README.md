# Pacific Rim — Linux Recompilation

A native Linux port of the Xbox 360 game **Pacific Rim: The Video
Game** (Yuke's, 2013 — the Jaeger-vs-Kaiju fighting game based on the
Warner Bros. / Legendary film), built with the
[ReXGlue SDK](https://github.com/ReXGlue/rexglue-sdk) static
recompiler — the same toolchain family as
['Splosion Man](https://github.com/thefixinhixon/SplosionManRecomp),
[The Maw](https://github.com/thefixinhixon/TheMawRecomp),
[Real Steel](https://github.com/thefixinhixon/RealSteelRecomp) and
[Condemned 2](https://github.com/thefixinhixon/Condemned2Recomp).

**Status: playable on Linux, with DLC and saves working** — boot,
menus, story fights and versus verified by playing, on Kubuntu
(AMD RX 6600 / RADV), with graphics, PipeWire audio, controller,
FSR upscaling, all six DLC addons, and save data that survives
relaunches (see "the save fix" below — it was the port's hardest
bug). **Windows build: not started** — the workflow in
`.github/workflows/windows.yml` follows the same recipe as the
other house ports and will produce the first test build.

> **No game data is included in this repository or its releases.**
> You must supply your own legally obtained copy of the game: the
> Xbox 360 package for title ID **584112C1**, or a folder containing
> `default.xex` extracted from it. The launcher (below) can import
> and extract the package for you. Pacific Rim is © Yuke's /
> Warner Bros. / Legendary. This is a fan-made interoperability
> project; do not redistribute game assets.
>
> The game's DLC (extra Jaegers and Kaiju) was sold separately on
> Xbox Live and is **not** part of the base package — but if you
> have the addon packages, the launcher installs them too (below).
> Online play is not supported (the Xbox Live services it used are
> gone); local play is the target.

## Download

Linux builds ship as an AppImage plus a folder zip (see Releases):
extract, `chmod +x` the AppImage, run — the launcher auto-detects
your game folder (`~/Games`, mounted drives, next to the launcher)
or lets you pick it, and its **Import XBLA Package** button extracts
a stock package into a ready-to-play folder. Requirements: a distro
with glibc 2.43 (the current packages are built on a recent
toolchain — older-distro rebuilds are on the roadmap), Vulkan
drivers for your GPU, and PipeWire or PulseAudio. A gamepad is
strongly recommended.

## The launcher

The **Shatterdome Launcher** is the house TP launcher shared with
the other ports (`tp-launcher/` in this repo, per-game themed from
one codebase — this build wears hangar steel and burnt orange,
with original banner and icon art):

- **Game tab**: detection status, one-click package import
  (built-in STFS extractor — no external tools), **DLC import**,
  PLAY.
- **Import DLC package or archive...** installs downloadable
  content (single packages, folders, or .rar/.zip/.7z archives of
  them) into the save data folder, writing each package's content
  header **with the license mask aggregated from the package
  itself** — the step that makes the game treat the content as
  owned. If no save data folder is set, the launcher sets one next
  to the game data and tells you.
- **Settings in four explained sections** (Graphics / Audio /
  Storage & Logs / Advanced), each with a plain-language note on
  what it does: resolution scale, FSR or bilinear presentation,
  FXAA, MSAA, VSync, frame limit, pipeline threads, audio gain and
  routing, your choice of save-data and log locations — plus a
  **Reset to Defaults** button.
- Settings persist between runs; the exact command line assembled
  for every launch is written to `last-command.txt` next to the
  logs.

Title-specific defaults worth knowing about:

- **VSync defaults OFF for this game**, the same engine behaviour
  as its sibling Real Steel: every verified launch of this port
  runs with it off and the runtime's frame limiter pacing the
  game. You can turn it back on in Settings to experiment.
- **First launch in a fresh data folder compiles shaders as you
  play**; a title-screen backdrop can take a few seconds to appear
  the very first time. It is cached after that.

## Porting notes (the three bugs that mattered)

1. **Byte-table dispatches.** rexglue's codegen under-analysed a
   dispatch idiom this engine uses everywhere (`lbzx` byte-class
   table + `bctr`): it emitted only the class-0 target and a trap
   for everything else, so boot threads died silently behind the
   runtime's fault handler and the game hung on a black screen.
   All 23 sites were translated instruction-for-instruction from
   disassembly and patched by `scripts/fix-switch-*.py` (wired
   into `scripts/refix.sh`, run after every codegen).
2. **The save fix.** The game integrity-hashes its save data with
   the Xbox kernel's XeCrypt SHA-512 — which the SDK had left as
   stubs, so no digest was ever computed and the game rejected
   *every* save, including fresh ones it had just written, as
   "corrupted". `patches/house-sdk.patch` now carries real
   SHA-512/SHA-384 implementations (FIPS 180-4, host-tested
   including the game's small-chunk update pattern). Saves written
   before this fix stay invalid; new ones verify properly.
3. **DLC headers.** As with Real Steel, extracted DLC only counts
   as owned when each package's `.header` record carries its
   aggregated license mask. The launcher's DLC importer does this
   for you; the standalone tooling lives in this project's history
   (the same recipe first proven on Real Steel's 18 addons).

## Building from source

You need: the ReXGlue SDK (pinned commit, with the house patches
and FidelityFX enabled), clang 20, Qt6 dev, Vulkan headers, and
**PipeWire + SPA development headers** — without them SDL silently
builds *without* a PipeWire audio backend and the game has no
sound on PipeWire systems. (Ask us how we know.)

1. Clone the SDK at `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`,
   init its submodules, and apply `patches/house-sdk.patch`
   (`git apply` — the accumulated house fixes, including the
   XeCrypt SHA-512 work and the content-system fixes).
2. Codegen from your own `default.xex` (or use the included
   `generated/`): `rexglue codegen`, with the function hints in
   `pacificrim_config.toml`. **If you re-run codegen you must redo
   the post-processing** before the tree will build and play: run
   `scripts/refix.sh` (it freezes the codegen rule in the build
   graph and applies the tail-call, include and byte-table
   dispatch fixes). The `generated/` tree in this repo is already
   in that post-fix state — it's the tree that produced the
   shipping Linux build.
3. Build the game: CMake preset `linux-amd64-release` on Linux,
   `win-amd64-release` on Windows, with
   `-DREXSDK_DIR=<sdk tree> -DREXGLUE_ENABLE_FIDELITYFX=ON`.
   (FidelityFX defaults OFF and fails *silently* — the game just
   looks blurry. Check for it.)
4. Build the launcher in `tp-launcher/` (CMake, Qt6,
   `-DTP_GAME=pacificrim`).

Note: **never mix** the executable and `librexruntime.so` /
`librexgpu-xenos.so` (or their Windows `.dll` counterparts) from
different build sets — it corrupts the heap.

## Known landmine: `/dev/shm` leaks (Linux)

The runtime backs guest memory with a ~4.8 GB `xenia_memory_*`
file in `/dev/shm` per run. Crashed or killed runs **leak** these
files; when `/dev/shm` fills, new runs die with a bus error (exit
code 7) immediately after "Guest memory arena mapped". The
launchers vacuum stale files before launching; if you're running
the game directly, check `df -h /dev/shm` and remove stale files
belonging to dead processes: `rm /dev/shm/xenia_memory_*`.

## Credits & disclosure

Full attribution for every library, tool and asset this project
uses — ReXGlue SDK, Xenia, Qt, 7-Zip, FidelityFX, FFmpeg, SDL and
more — lives in **[CREDITS.md](CREDITS.md)**, with license texts
in **[licenses/](licenses/)**. This project's own code is
**BSD 3-Clause** (see [LICENSE](LICENSE)), matching the ReXGlue
SDK's license; the game itself remains © Yuke's / Warner Bros. /
Legendary and no game data is distributed.

- **Yuke's** — for Pacific Rim, and for making giant robots
  punching giant monsters feel appropriately enormous.
- **ReXGlue SDK team and upstream rexglue contributors** — the
  recompiler and runtime that make this possible.
- The launcher is an original, clean-room design shared across
  the house ports (extraction, detection and settings model
  informed by the earlier launcher work on the Dante's Inferno
  and Condemned 2 recompilations).
- This port was built by Jason Hixon with heavy AI assistance
  (Muse, by Meta) on the coding side, and tested the
  old-fashioned way: by playing it. The original release's DLC
  practices did this game dirty; this port is the receipt —
  everything unlocked, everything working, no store required.

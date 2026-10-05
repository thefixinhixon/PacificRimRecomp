# Credits & Attribution

PacificRimRecomp stands on a tall stack of other people's work. This
file credits every project, library and tool the port uses, and the
license each one is under. License texts live in `licenses/`.
This project's own code is BSD 3-Clause (see `LICENSE`).

## The game

- **Pacific Rim: The Video Game** (Xbox 360, 2013) — developed by
  **Yuke's**, based on the Warner Bros. Pictures / Legendary Pictures
  film; © Yuke's / Warner Bros. / Legendary.
  **No game data is included** in this repository or its releases;
  players supply their own legally obtained copy. Nothing in this
  project is affiliated with or endorsed by Yuke's, Warner Bros.,
  Legendary or Microsoft.

## The recompiler and runtime

- **ReXGlue SDK** — Copyright (c) 2026, **Tom Clay**
  <tomc@tctechstuff.com> and contributors.
  https://github.com/rexglue/rexglue-sdk — **BSD 3-Clause**.
  The static recompiler that translated the game's code, and the
  runtime (`librexruntime` / `rexruntime.dll`, `librexgpu-xenos` /
  `rexgpu-xenos.dll`) the port runs on. This project's
  `patches/house-sdk.patch` is a derivative of the SDK and is
  distributed under the same license; the SDK's license text is
  reproduced verbatim in `licenses/ReXGlue-SDK-BSD-3-Clause.txt`.
  Two Pacific Rim-specific fixes live in that patch: real
  implementations of the XeCrypt SHA-512/SHA-384 kernel functions
  (the game's save-data integrity hash — with the SDK's original
  stubs, the game rejected every save as corrupted), and the
  content-enumeration and deferred-content fixes first written for
  Real Steel's DLC.
- **Xenia** — Copyright (c) 2022, **Ben Vanik** and the Xenia project
  contributors. https://xenia.jp — **BSD 3-Clause**.
  Portions of the ReXGlue SDK are derived from the Xenia Xbox 360
  emulator (per the SDK's own LICENSE, reproduced in the same file).

## Graphics and upscaling

- **FidelityFX SDK** (FSR upscaling) — Copyright (C) 2024,
  **Advanced Micro Devices, Inc.** — **MIT License**
  (`licenses/MIT.txt`).
- **Vulkan** headers and loader ecosystem — © **The Khronos Group** —
  Apache 2.0 / MIT (build-time headers; the loader and drivers are
  the user's own system components).
- **glslang**, **SPIRV-Tools**, **SPIRV-Headers** — © The Khronos
  Group and contributors — BSD 3-Clause / Apache 2.0
  (`licenses/Apache-2.0.txt`); vendored by the SDK, used in shader
  translation.
- **DirectXShaderCompiler (dxc)** shader libraries vendored by the
  SDK — Apache 2.0 with LLVM Exception / NCSA.

## Runtime libraries (vendored by the ReXGlue SDK)

These ship inside, or are linked into, the runtime binaries. Each
keeps its own license; the SDK's `thirdparty/` directory carries the
full texts.

| Component | Copyright / author | License |
|---|---|---|
| SDL3 | Sam Lantinga and the SDL contributors | zlib (`licenses/zlib.txt`) |
| FFmpeg | the FFmpeg developers | LGPL v2.1 or later (`licenses/FFmpeg-LGPL-2.1.txt`) |
| fmt | Victor Zverovich and {fmt} contributors | MIT (`licenses/MIT.txt`) |
| spdlog | Gabi Melman and contributors | MIT |
| Dear ImGui | Omar Cornut and contributors | MIT |
| CLI11 | Henry Schreiner and contributors | BSD 3-Clause |
| simde | Evan Nemerson and contributors | MIT |
| stb libraries | Sean Barrett and contributors | MIT / public domain |
| libmspack | Stuart Caie | LGPL v2.1 |
| tiny-aes-c / aes_128 | Kokke and contributors | public domain (Unlicense) |
| Catch2 (tests only) | Catch2 contributors | BSL-1.0 |
| MoltenVK (macOS only; not in Linux/Windows builds) | The Brenwill Workshop / Khronos | Apache 2.0 |

## The launcher

- **The TP house launcher** (here: the *Shatterdome Launcher*) is
  this project's own original code (BSD 3-Clause, `LICENSE`), shared
  across the house ports ('Splosion Man, The Maw, Real Steel,
  Pacific Rim).
- **Qt 6** — © **The Qt Company Ltd.** and the Qt Project
  contributors — **GNU LGPL v3** (`licenses/LGPL-3.0.txt`).
  The launcher links Qt as shared libraries; the AppImage bundles
  those unmodified shared libraries, and they can be replaced or
  relinked as the LGPL describes.
- **7-Zip (7zz)** — Copyright (C) 1999-2024, **Igor Pavlov** —
  GNU LGPL with the unRAR license restriction for some code, and
  BSD 2-/3-Clause for other parts; 7-Zip's own license text is
  reproduced verbatim in `licenses/7-Zip-License.txt`.
  An unmodified official 7-Zip binary is bundled next to the
  launcher in the release packages (7zz on Linux, 7z.exe + 7z.dll
  on Windows) and used only to unpack user-supplied
  .rar/.zip/.7z archives during game import. Source:
  https://www.7-zip.org/
- Launcher banner and icon artwork are original illustrations
  created for this project and are distributed under the project's
  BSD 3-Clause license.

## Packaging and build tools (build-time only)

- **linuxdeploy** and **linuxdeploy-plugin-qt** — MIT License.
  Used to assemble the AppImage.
- **appimagetool / AppImageKit** — MIT License. The AppImage runtime
  it embeds is part of every distributed .AppImage file.
- **Clang / LLVM** — Apache 2.0 with LLVM Exception. The compilers
  that build the project.
- **CMake** (BSD 3-Clause) and **Ninja** (Apache 2.0) — the build
  system.

## People

- **Jason Hixon** — the port: project setup, testing (all of it, the
  old-fashioned way — by playing), packaging and releases.
- Built with heavy **AI assistance (Muse, by Meta)** on the coding
  side: codegen triage, the launcher, packaging and this file's
  research included. The bugs were real; the robots were factual.
- The ReXGlue and Xenia communities, whose compatibility notes and
  issue trackers quietly saved hours more than once.

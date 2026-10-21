<div align="center">

# mgrr-nx

**Metal Gear Rising: Revengeance for Nintendo Switch**

An unofficial native Nintendo Switch port of **Metal Gear Rising: Revengeance** (Xbox 360, 2013),
made by static recompilation: no emulator.

[![Switch](https://img.shields.io/badge/Nintendo_Switch-Homebrew-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)](#)
[![Status](https://img.shields.io/badge/Status-Early_development_(M2)-orange?style=for-the-badge)](#status)
[![Architecture](https://img.shields.io/badge/Architecture-PowerPC_→_AArch64-0091BD?style=for-the-badge&logo=arm&logoColor=white)](#)
[![License](https://img.shields.io/badge/License-GPL--3.0-blue?style=for-the-badge)](LICENSE)

**English** | [Русский](README_RU.md)

</div>

---

## About

`mgrr-nx` turns the Xbox 360 executable of **Metal Gear Rising: Revengeance** into native AArch64 code for the
Nintendo Switch (Horizon OS). The PowerPC code of the game is translated to C++ ahead of time and compiled with clang;
the Xbox 360 kernel, file system, audio and threads are provided by the runtime; the game's GPU commands are read
from its command ring and drawn with **deko3d**, the Switch's native graphics API.

There is no emulator at play time: the game's own code runs as a normal Switch program.

**This repository contains no game code or assets.** The port is built on your machine from your own Xbox 360 disc
of the game (TitleID `4B4E080A`, `default.xex` SHA-256 `907c8ebe94f085924f230d815ffa0b89c65ce60adfd00184b047276954ddf443`).
The recompiled C++, the translated shaders and every game file stay local and are never committed or distributed.

---

## Status

**The game is not playable yet.** On a Switch it boots, plays sound, answers the controller and runs its logic,
including gameplay, but the screen is black: the renderer is being built now.

| Milestone | Goal | State |
| :--- | :--- | :--- |
| **M0 Foundation** | Recompilation project, Linux and Switch builds from the disc image | ✅ done |
| **M1 Boot** | The game runs on PC and on the Switch | ✅ done: PC shows the title screen (reference GPU emulation); the Switch runs without a picture |
| **M2 Graphics** | Native deko3d renderer: main menu and the start of R-00 match the Xbox 360 | 🔧 in progress |
| **M3 Vertical slice** | Input, audio, cutscenes, saves: the prologue R-00 playable from start to end | planned |
| **M4 60 FPS** | Profiling and optimization: R-00 at 60 FPS on an overclocked Erista Switch | planned |

The first target is a vertical slice, the prologue **R-00**, at **60 FPS on any Switch with overclocking**. When the
frame budget is tight, the load is reduced (resolution, shadows, particles, draw distance), never the frame rate.

### Measured so far (Switch, Erista, stock 1020 MHz CPU, no rendering yet)

| Scene | FPS | Draws per frame | CPU |
| :--- | :--- | :--- | :--- |
| Logos | 60 | 6 | 113 % of 300 % |
| Title screen (capped at 30 by the game) | 22 | 390–560 | 245 % |
| Gameplay | 37–48 | 280–385 | 200 % |

Stack samples showed the threads were mostly waiting on one global lock that the recompiler put around the Xbox
360's atomic operations; it is now removed (`skip_msr`) and waiting for the next console measurement. Details and
every measurement: [NOTES.md](NOTES.md).

### Done in M2 so far

- Shader library: all 1492 shaders of the game go through Xenos microcode → HLSL → SPIR-V → DKSH (the console's
  shader binary); 1482 make it, including every shader the title screen uses.
- Shader identification: pixel shaders by their microcode (88 of 88 on the title screen), vertex shaders through
  a hook on the game's shader creation (every shader the game creates is found, except the 10 untranslated ones).
- Next: the GPU ring decoder that turns each of the game's draws into a request for the renderer, then the first
  frame of the main menu.

---

## Built with

- **[rexglue-sdk-nx](https://github.com/nx-mod/rexglue-sdk-nx)** (submodule `sdk/`, BSD-3-Clause): the ReXGlue static
  recompiler and runtime (derived from Xenia), its Horizon OS layer, and the universal deko3d renderer from
  [xbox360-nx](https://github.com/nx-mod/xbox360-nx).
- **XenosRecomp** ([hedge-dev](https://github.com/hedge-dev/XenosRecomp), MIT) with the changes of the NFS Most
  Wanted port in xbox360-nx: Xbox 360 shaders to HLSL (`tools/shaders/xenosrecomp/`).
- **DXC** (HLSL → SPIR-V), **SPIRV-Cross** and **uam** (SPIR-V → GLSL → DKSH), **deko3d** and **libnx** (devkitPro).
- **clang** for the recompiled game code on both platforms, **devkitA64 GCC** for the runtime and linking on Switch.
- On PC, the reference renderer is the Xenia GPU emulation that ReXGlue ships; it is used to compare against.

---

## Building

> These steps are for development: there is nothing to play yet.

### Requirements

- Linux, `clang`, `cmake`, `ninja`, `python3`, Docker (for the `devkitpro/devkita64` image).
- Your own Xbox 360 disc image of Metal Gear Rising: Revengeance.
- For the shader library: a DXC binary (Vulkan SDK or `directx-shader-compiler`).

### Steps

```bash
git clone --recursive https://github.com/malinkin-s/mgrr-nx.git
cd mgrr-nx

# Extract the disc, check default.xex, run the ReXGlue codegen and build the Linux binary
tools/build_from_iso.sh /path/to/MGRR.iso

# Run on PC (Xenia GPU emulation as the reference renderer)
tools/run_linux.sh

# Shader library: build/shaders/mgrr_shaders.mgsl
DXC=/path/to/dxc tools/shaders/build_library.sh
```

The Switch build (`.nro`) is configured in the `devkitpro/devkita64` container with
`tools/switch_build/switch-devkitA64.cmake`; `tools/switch_build/clang_recomp.sh` then compiles the game code with
clang and links `mgrr_clang.nro`. The exact commands are in [NOTES.md](NOTES.md) (M0.4, M1).

### Running on a Switch

Atmosphère, title takeover mode (hold **R** while starting a game; the port needs about 3.2 GB of memory):

```text
sdmc:/switch/mgrr/
├── mgrr_clang.nro
├── mgrr_shaders.mgsl     # your shader library
└── game/                 # the files of your disc: default.xex, media/...
```

Logs go to `sdmc:/switch/mgrr/logs/`.

---

## Repository layout

```text
mgrr-nx/
├── game/                 # the port: ReXGlue manifest, our analysis of the executable, hooks, GPU ring reader
│   └── src/
├── tools/
│   ├── build_from_iso.sh # disc image → recompiled C++ → Linux build
│   ├── shaders/          # shader library pipeline (translator, packer, DKSH probe)
│   ├── switch_build/     # Switch toolchain file and the clang build of the game code
│   └── sdk_patches/      # our fixes to rexglue-sdk-nx until they are upstream
├── x360_work/            # reverse-engineering notes: D3D, formats, rendering (addresses and our notes only)
├── sdk/                  # rexglue-sdk-nx (submodule)
├── PLAN.md               # scope, constraints, milestones, decision log
└── NOTES.md              # research and measurement log
```

---

## Credits

- [rexglue-sdk-nx](https://github.com/nx-mod/rexglue-sdk-nx) and [xbox360-nx](https://github.com/nx-mod/xbox360-nx)
  (nx-mod): the runtime, the Switch layer and the deko3d renderer this port is built on.
- [ReXGlue](https://github.com/rexglue/rexglue-sdk) and [Xenia](https://github.com/xenia-project/xenia): the recompiler and the
  emulator research it comes from.
- [hedge-dev](https://github.com/hedge-dev): XenosRecomp, XenonRecomp and UnleashedRecomp.
- [devkitPro](https://devkitpro.org): libnx, deko3d, uam and the toolchain.

Metal Gear Rising: Revengeance is © Konami Digital Entertainment, developed by PlatinumGames. This project is not
affiliated with or endorsed by them.

## License

[GPL-3.0](LICENSE).

<div align="center">

# mgrr-nx

**Metal Gear Rising: Revengeance for Nintendo Switch**

An unofficial native Nintendo Switch port of **Metal Gear Rising: Revengeance** (Xbox 360, 2013),
made by static recompilation: no emulator.

[![Switch](https://img.shields.io/badge/Nintendo_Switch-Homebrew-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)](#)
[![Status](https://img.shields.io/badge/Status-Archived-lightgrey?style=for-the-badge)](#status)
[![Architecture](https://img.shields.io/badge/Architecture-PowerPC_→_AArch64-0091BD?style=for-the-badge&logo=arm&logoColor=white)](#)
[![License](https://img.shields.io/badge/License-GPL--3.0-blue?style=for-the-badge)](LICENSE)

**English** | [Русский](README_RU.md)

</div>

> [!NOTE]
> **This project is archived (October 2026).** It stopped at milestone M2: the game boots and renders the title
> screen, training and mission R-00 on the Switch, with our own PM4-level renderer on deko3d. For a playable port see
> **[mgr-nx-deko](https://github.com/tuancapoo/mgr-nx-deko)**. The fixes and tools here stay available under the
> GPL-3.0: the SDK patches in `tools/sdk_patches/` (among them `0011`, guest memory never given back, which hung
> the game after the first boss), the Xenos multiply rule in `tools/shaders/xenosrecomp/`, the graphics test bench
> (`tools/emu_test.sh`) and the findings in [NOTES.md](NOTES.md).

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

**Not ready to play yet, but it renders.** On a Switch the game boots, plays sound, answers the controller, and the
native deko3d renderer draws it in full: the title screen with its 3D scene and effects, the menus, the VR training
and the first mission R-00 with enemies, combat, particles and the HUD. Known issues: black flicker on Raiden's face,
stutters, and the frame rate in missions (about 38–45 FPS with overclocking, see below).

| Title screen | VR training |
| :---: | :---: |
| ![Title screen](docs/screenshots/title.jpg) | ![VR training](docs/screenshots/training.jpg) |
| **R-00** | **Combat** |
| ![Mission R-00](docs/screenshots/mission.jpg) | ![Combat](docs/screenshots/combat.jpg) |

<sub>Captured on a Switch (Erista) with the port's debug overlay in the corner, October 2026.</sub>

| Milestone | Goal | State |
| :--- | :--- | :--- |
| **M0 Foundation** | Recompilation project, Linux and Switch builds from the disc image | ✅ done |
| **M1 Boot** | The game runs on PC and on the Switch | ✅ done: PC shows the title screen (reference GPU emulation) |
| **M2 Graphics** | Native deko3d renderer: main menu and the start of R-00 match the Xbox 360 | 🔧 in progress |
| **M3 Vertical slice** | Input, audio, cutscenes, saves: the prologue R-00 playable from start to end | planned |
| **M4 60 FPS** | Profiling and optimization: R-00 at 60 FPS on an overclocked Erista Switch | planned |

The first target is a vertical slice, the prologue **R-00**, at **60 FPS on any Switch with overclocking**. When the
frame budget is tight, the load is reduced (resolution, shadows, particles, draw distance), never the frame rate.

### Measured so far (Switch, Erista, with rendering)

| Scene | FPS | Draws per frame | GPU per frame |
| :--- | :--- | :--- | :--- |
| Title screen and menus (capped at 30 by the game) | 30 | ~490 | 18 ms |
| VR training | ~60 | 360–400 | — |
| Mission R-00 (CPU 1785 MHz, GPU 921 MHz) | 38–45 | 800–1400 | 12–15 ms |

The GPU fits in a 60 FPS frame; the CPU side is next. Details and every measurement: [NOTES.md](NOTES.md).

### Done in M2 so far

- Shader library: all 1492 shaders of the game go through Xenos microcode → HLSL → SPIR-V → DKSH (the console's
  shader binary); 1482 make it, including every shader the title screen uses.
- Shader identification: pixel shaders by their microcode (88 of 88 on the title screen), vertex shaders through
  a hook on the game's shader creation (every shader the game creates is found, except the 10 untranslated ones).
- GPU ring decoder: each of the game's draws becomes a request to the deko3d renderer, including the clears Direct3D
  does by drawing with its own shaders, and the multisampled surfaces it aliases.
- Shaders linked by interpolator slot, as the Xenos does (not by semantic): this brought back the red glow of the
  title screen and other effects.
- Renderer fixes merged from [ds2-nx](https://github.com/VasilkovND/ds2-nx) (more render target and resolve formats,
  signed textures, partial resolves, the frame dump).
- Graphics test bench: frames recorded on PC are replayed through the Switch renderer (in Ryujinx and on the console)
  and compared with Xenia's image, frame by frame and resolve by resolve. The title screen is within 1.4 % of the
  reference (pixels off by more than 32 of 255). The console stays the target; the emulator only filters out errors.
- Vertex formats: packed 10:11:11 normals are decoded in the shaders.
- Next: the flicker on Raiden's face, then frame rate.

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

### Debugging

- `tools/nxlink.sh [SWITCH_IP]` sends the build over the network (hbmenu: press **Y**) and receives its log on the PC,
  in English, together with screenshots (every 30 s and on **L+R+Minus**): `build/nxlink_logs/<time>/`.
- `--gpu_trace_path=FILE --gpu_trace_start=N --gpu_trace_frames=K` on the PC build records K frames of GPU commands
  and the memory they read, from frame N (`--gpu_trace_start=0`: only when **F9** is pressed). With
  `--readback_resolve=full` Xenia's result of every resolve is kept too. `python3 -I tools/gputrace.py FILE`
  summarizes a recording.
- `tools/emu_test.sh FILE [PASSES [FRAME]]` replays a recording in Ryujinx with the Switch build and compares it with
  the reference; with `FRAME`, every resolve and draw of that frame is listed and saved (`PROBES=`, `PLAIN=` and
  `tools/resolve_ref.py` narrow a difference down to one draw). The same build replays a recording on the console:
  `tools/nxlink.sh IP -- --mgrr_replay=sdmc:/switch/mgrr/traces/FILE`.

---

## Repository layout

```text
mgrr-nx/
├── game/                 # the port: ReXGlue manifest, hooks, GPU ring reader and PM4 decoder
│   └── src/
├── tools/
│   ├── build_from_iso.sh # disc image → recompiled C++ → Linux build
│   ├── shaders/          # shader library pipeline (translator, packer, DKSH probe)
│   ├── switch_build/     # Switch toolchain file and the clang build of the game code
│   ├── nxlink.sh         # network launch with the log and screenshots sent back
│   ├── gputrace.py       # summary of a GPU recording
│   ├── emu_test.sh       # graphics test bench: replay in Ryujinx and compare with the reference
│   ├── imgdiff.py        # image comparison for the bench
│   ├── resolve_ref.py    # reference images of each resolve of a recording
│   └── sdk_patches/      # our fixes to rexglue-sdk-nx until they are upstream
├── docs/screenshots/     # console screenshots for this page
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
- [ds2-nx](https://github.com/VasilkovND/ds2-nx) (VasilkovND): renderer fixes (`tools/sdk_patches/0009-…`) and the
  emulation of Direct3D's clears, from its notes and code.
- [devkitPro](https://devkitpro.org): libnx, deko3d, uam and the toolchain.

Metal Gear Rising: Revengeance is © Konami Digital Entertainment, developed by PlatinumGames. This project is not
affiliated with or endorsed by them.

## License

[GPL-3.0](LICENSE).

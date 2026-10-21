# XenosRecomp (vendored)

Xbox 360 shader microcode → HLSL. [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev (MIT, `LICENSE.md`),
as modified by the NFS Most Wanted port in [xbox360-nx](https://github.com/nx-mod/xbox360-nx) (`shaders/`, commit 6575330, GPL-3):
their changes are under `NFSMW_RECOMP`, and `shader_common.h` has the deko3d mode (`NFSMW_DEKO3D`) that the SDK renderer
(`rex::x360::RenderDk`) expects. Copied unchanged; only the translation step is used (no DXC API, smol-v or zstd).

Our driver is `../mgrr_hlsl.cpp`; the pipeline is `../build_library.sh`.

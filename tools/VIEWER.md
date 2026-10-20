# Model viewer (verification tool)

    venv/bin/python tools/viewer.py MODEL [--out out.png] [--views 0,35,90,180] [--size 520] [--gain 2.0] [--alpha-test]
                                    [--role N] [--no-tex] [--cull] [--orphans] [--all-meshes] [--list] [--window]

* `MODEL` is a base name like `pl0100`, `em0010`, `ba0020` (folder guessed from the prefix, otherwise searched). Needs `numpy pillow moderngl` (+ `glfw` for `--window`) in a venv; offscreen rendering uses EGL.
* Loads the WMB and its WTA/WTP from the CPK archives (`tools/model_loader.py`); textures missing from the model's own files are resolved by id — shared lookup textures from `core.dtt::pre_texture.wtb`
  (table flag 0x63) and level textures through the global index `x360_work/textures_index_all.json`.
* Albedo = the material binding with role 0 (`--role` changes it). Lighting is two directional lights + hemisphere ambient; the game's own shaders are not run. Winding is Direct3D clockwise.
* By default it skips batches that no mesh lists and the damage/dismemberment variants (`nocut_`, `cut-`, `danmen`, `_dam1/2`, `blood`); `--orphans` / `--all-meshes` draw them.
* `--alpha-test` discards texels with alpha < 0.5 — right for foliage cards (ba0020), WRONG for characters, whose albedo alpha carries other data (holes appear). The correct per-material rule depends on the shader key (not decoded yet). Window mode: left drag orbits, wheel zooms (written, not exercised in this session).

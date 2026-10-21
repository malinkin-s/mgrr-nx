# Шейдеры MGR:R (Xbox 360) → библиотека для рендерера deko3d

Одна команда из извлечённого диска (`x360_extracted/`):

```
DXC=/путь/к/dxc tools/shaders/build_library.sh [JOBS]
```

Результат — `build/shaders/mgrr_shaders.mgsl`. Это данные из игры, поэтому в git они не попадают. Файл кладётся рядом с исполняемым файлом: на Switch в `sdmc:/switch/mgrr/`, путь можно сменить опцией `--mgrr_shader_library`.

| Шаг | Чем |
|---|---|
| `data000.cpk` → `shader.dat`, `shader2.dat`, `shadereff.dat` → 1492 контейнера | `tools/cpk.py`, `tools/pgdat.py` |
| контейнер → HLSL | `mgrr_hlsl.cpp` + `xenosrecomp/` (XenosRecomp с правками xbox360-nx, режим deko3d) |
| HLSL → SPIR-V | DXC: `-T vs/ps_6_6 -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Werror=parameter-usage -DNFSMW_DEKO3D` |
| упаковка и проверка тем же кодом, что в игре | `library/mgrr_pack_shaders.cpp` + `game/src/mgrr_shader_library.cpp`, `game/src/mgrr_shaders.cpp` |
| SPIR-V → GLSL → DKSH, путь компиляции консоли | `dksh_probe/` (SDK `SpirvAGlsl` + uam) |

С флагом `--xsh` упаковщик ищет в библиотеке микрокод, который игра реально загружала при запуске на PC (кэш шейдеров Xenia, `~/.local/share/mgrr/cache/shaders/shareable/4B4E080A.xsh`). Скрипт передаёт этот флаг сам, если файл есть.

Старые файлы `shaderlib.py` и `dxc_one.sh` — для офлайн-просмотрщика моделей (`tools/vkview`, `x360_work/REAL_SHADERS.md`). Они работают через upstream XenosRecomp и Vulkan, а не через deko3d.

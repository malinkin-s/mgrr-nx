# Шейдеры MGR:R (Xbox 360) -> HLSL -> SPIR-V

1. Достать DAT из CPK и распаковать (CRILAYLA с затёртой сигнатурой, см. tools/cpk.py):
   `python3 tools/cpk.py x x360_extracted/media/data000.cpk x360_work/shaders '(^|/)(shader.*|ShaderSign)\.(dat|bxm)$'`
2. Распаковать DAT (tools/pgdat.py): shader.dat (1029), shader2.dat (83), shadereff.dat (428):
   `python3 tools/pgdat.py x shader.dat OUT`
3. XenosRecomp (hedge-dev/XenosRecomp@990d03b, собран clang+cmake): `XenosRecomp in.vso out.hlsl shader_common.h`
4. DXC (thirdparty/dxc-bin, LD_LIBRARY_PATH=.../lib/x64): `dxc-linux -spirv -T vs_6_0|ps_6_0 -E main -fspv-target-env=vulkan1.2 -HV 2021` (tools/shaders/dxc_one.sh)

Режим каталога XenosRecomp (`XenosRecomp DIR out.cpp shader_common.h`) падает с segfault из-за 4 шейдеров частиц — используйте пофайловый режим.

# Пробная сборка рекомпилированного MGR:R под AArch64 (devkitA64)

Образ: devkitpro/devkita64 (GCC aarch64-none-elf). Сгенерированный XenonRecomp код написан под clang,
поэтому для GCC нужен gcc_compat.h (`__builtin_assume`, `__builtin_rotateleft64/32`, `__builtin_debugtrap`).

    docker run --rm -e PATH=/opt/devkitpro/devkitA64/bin:/usr/bin:/bin \
      -v $PWD/x360_work/ppc:/ppc:ro -v <XenonRecomp>/thirdparty/simde:/simde:ro -v <outdir>:/obj \
      -v tools/switch_build/compile_in_devkita64.py:/cc.py:ro -v tools/switch_build/gcc_compat.h:/compat.h:ro \
      -v tools/switch_build/runall.sh:/runall.sh:ro devkitpro/devkita64 sh /runall.sh

Результат -O2 (GCC 14, cortex-a57): 513 файлов + ppc_func_mapping.cpp, все собираются; см. NOTES.md.

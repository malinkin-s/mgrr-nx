# S0: микробенчмарк перекомпилированного кода на Switch

Цель (PLAN.md, S0): ранний сигнал по скорости — «гостевые MIPS» реального кода игры на A57 — и проба, как разместить 4 ГБ гостевой памяти на Horizon.

1. `select.py OUTDIR` — все функции-листья с циклами из `x360_work/ppc` (2331 шт.) с маркером на каждую гостевую инструкцию.
2. Сборка с `-DS0_COUNT` и `main.cpp`, затем `s0_count sweep x360_work/mgrr_image.bin`: каждая функция запускается в отдельном процессе 3 раза на синтетических данных (скретч 4 МБ, указатели внутрь него, небольшие числа, float). Детерминированные функции с объёмом работы от 5 тыс. до 50 млн инструкций попадают в `selected.txt` (96 функций, из них 12 векторных).
3. `S0_PPC=<папка рекомпиляции> select.py GEN/<вариант> selected.txt 8 <вариант>` для вариантов base (x360_work/ppc), opt и optnv (x360_work/ppc_opt, собран из mgrr_opt.toml), затем `build.sh pc|arm|switch GEN OUT [count]` (switch — в контейнере devkitpro/devkita64, arm — в arm64-контейнере через qemu). optnv собирается с novol/ppc_context.h (без volatile) и -fno-strict-aliasing.

На Switch: `sdmc:/switch/s0bench/{s0bench.nro, mgr_image.bin}` (образ из своего диска: x360_work/mgrr_image.bin), лучше запускать в режиме title takeover.
Результат: `sdmc:/switch/s0bench/result_<МГц>mhz.txt`.

Память: G (как в UnleashedRecomp-NX и rexglue-sdk-nx: куча → svcMapProcessCodeMemory → svcMapProcessMemory в окно 4 ГБ из ASLR), если не вышло — F. Пробы: A (`svcMapPhysicalMemory` в alias) и B (`svcMapMemory` в alias) только пробуются и пишутся в отчёт; рабочий способ — F:
гостевое окно внутри кучи hbl: гостевые [0, 64 МБ) и образ 0x82000000..0x83A40000, а промежуток [64 МБ, 0x82000000) — куча самой программы (malloc).

Первый прогон на Erista (1020 МГц, title takeover, 2026-10-20): A → 0xFA01 InvalidState (SystemResourceSize = 0 у процесса hbl),
B → 0xDC01 InvalidMemoryRegion, stack-регион 2 ГБ — меньше нужных 2,06 ГБ, malloc одного блока 2,06 ГБ — отказ. Поэтому добавлен F.

Оговорки: синтетические входы; 6 векторных функций — копирование памяти на 16 МБ (меряют память, а не CPU), поэтому основная метрика — медиана и геометрическое среднее.
PC (i3-12100F, clang -O2): медиана 5639 MIPS, geomean 5850, aggregate 501 (из-за копирований).

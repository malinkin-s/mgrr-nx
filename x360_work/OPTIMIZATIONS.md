# Кандидаты в оптимизации CPU (поиск 2026-10-20)

Статус: **проверено** = измерено в S0/S0b на Erista; **гипотеза** = не мерили.

| # | Что | Статус / ожидание | Источник |
|---|---|---|---|
| 1 | Опции XenonRecomp `skip_lr`, `skip_msr`, `ctr/xer/reserved/cr/non_argument/non_volatile_as_local` | **проверено**: ×1,16–1,20 (медиана на функцию) | UnleashedRecomp `SWA.toml` включает все |
| 2 | Доступ к гостевой памяти без `volatile` (+ `-fno-strict-aliasing`) | **проверено**: вместе с п.1 ×1,53 | наш эксперимент; UnleashedRecomp(-NX) оставляет `volatile` |
| 3 | Нативные замены горячих функций (`memcpy`/`memmove`/`memset`, позже — по профилю) через хуки | гипотеза; копирования в S0 — самые медленные функции (≈30–110 MIPS на PC) | UnleashedRecomp `misc_impl.cpp`: 10 хуков memcpy/memmove/memset, всего 275 `GUEST_FUNCTION_HOOK` |
| 4 | Переключения режима flush-to-zero (`msr fpcr`) | гипотеза: в коде 73 525 `disableFlushMode` + 11 509 безусловных переключений; на A57 запись FPCR дорогая. Варианты: глобальный режим, перенос на границы функций | счёт по `x360_work/ppc_opt` |
| 5 | Clang вместо GCC для кода игры на Switch (sysroot devkitA64) | гипотеза; на PC clang и GCC свернули разные циклы | BOTW-декомпиляция собирает Switch-код clang'ом |
| 6 | `-O3`, LTO для кода игры | гипотеза; LTO на 270 МБ C++ может быть очень долгим | — |
| 7 | PGO (`-fprofile-generate/-use`) по реальному прохождению R-00 | гипотеза; нужен вывод профиля на SD | — |
| 8 | Точность FP: `-frounding-math` (GCC) / `-ffp-model=strict` (clang) | цена для скорости, а не ускорение; UnleashedRecomp включает ради корректности. Мерить, нужен ли MGR | UnleashedRecomp `UnleashedRecompLib/CMakeLists.txt` |
| 9 | Ленивая перестановка байт / устранение пар bswap | гипотеза, дорого (менять XenonRecomp); на ARM `rev` дешёвый — выигрыш, скорее всего, мал | патенты Transitive (lazy byteswapping) |
| 11 | Прямые вызовы `__imp__sub_X` вместо weak-алиасов `sub_X` (иначе GCC не инлайнит, LTO бесполезен) | гипотеза для нас; у xbox360-nx вместе с LTO+PGO+порядком функций 30,3 → ~34 кадров/с | xbox360-nx `docs/toolchain.md` |
| 12 | Хуки на busy-wait'ы D3D-слоя игры (ожидание GPU, `Sleep(0)`-опрос) | у xbox360-nx освобождали 2 аппаратных потока | xbox360-nx `docs/platform-notes.md` |
| 10 | Распределение потоков по ядрам 0–2, приоритеты; ядро 3 — системное | обязательная работа в M1, не ускорение кода | libnx |

## Память под hbl (замена способа F)
UnleashedRecomp-NX получает настоящее разреженное окно 4 ГБ без NSP: `virtmemFindAslr(4 ГБ)` → для каждого диапазона
`memalign` из кучи → `svcMapProcessCodeMemory` (в code-регион) → `svcSetProcessMemoryPermission(RW)` → `svcMapProcessMemory` в окно;
страницы подключаются по требованию (`CommitRange`). Нужны «подсказки» syscalls 0x74/0x75/0x77/0x78 от hbl (`envIsSyscallHinted`).

## Ориентир
UnleashedRecomp-NX (Sonic Unleashed, тот же XenonRecomp + devkitA64 GCC, Vulkan через собственную сборку Mesa NVK + plume):
рекомендуют разгон и **30 кадров/с при ~400p**, жалобы на фреймпейсинг. Где узкое место (CPU или GPU) — не сообщается.

Подробные уроки похожих проектов: `PRIOR_ART.md`.

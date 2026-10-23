# Xbox 360 версия (mvl-mgrr.iso, XGD3)
- Образ: XDVDFS со смещением 0x2080000 (XGD3). Экстрактор: scratchpad/xdvdfs.py. Распаковано в x360_extracted/ (default.xex, media/*; $SystemUpdate удалён)
- default.xex: XEX2 retail, AES+LZX, TitleID 4B4E080A, база 0x82000000, entry 0x82302C28, образ 27 525 120 Б
- Расшифрованный образ: x360_work/mgrr_image.bin (полный образ с базы 0x82000000)
- Секции: .rdata 0x82000400, .pdata 0x821F3A00 (57 025 функций), .text 0x82270000 (0xF8D75C ≈16,3 МБ ≈ 4,08 млн инстр.), .data 0x83200000, .idata 0x839B0000
- Middleware: CRI Atom/ADX/ASR Sep 2012 (XBOX360), Wwise (AK::), Havok 3.1.0; 529 RTTI-типов
- XenonRecomp (hedge-dev, ddd128b) собран: scratchpad/XenonRecomp/build. XenonAnalyse находит 0 jump tables:
  в .text ~691 bctr с индексной загрузкой, паттерн: lis/addi/lwzx(lbzx) + lis/addi + add + mtctr + bctr (относительные таблицы),
  Sonic-паттерны не подходят -> нужно дописать детектор.

## Jump tables / границы функций (сделано)
- tools/find_jumptables.py IMAGE OUT.toml — детектор (символьное исполнение участка между `cmplwi/bgt` и `mtctr/bctr`).
  Результат: 596 таблиц из 1645 bctr; 499/499 проверяемых целиком внутри своей .pdata-функции (0 выходов за границы).
  Типы: 578 абсолютных (lwzx), 13 half-offset, 4 byte<<2, 1 byte. Остальное — трамплины/таблицы указателей на функции (не switch);
  1 нестандартный switch (8298C30C: граница по r11=r10|r11, индекс r10) — вручную.
- tools/switch_function_bounds.py — явные `functions=[...]` для 97 switch в функциях без .pdata (рекомпилятор режет их по таблице).
- tools/cap_overlapping_functions.py — ограничивает функции, которые анализатор XenonRecomp раздул поверх соседних (8 штук).
- x360_work/mgrr.toml — рабочий конфиг XenonRecomp (адреса save/rest GPR/FPR/VMX найдены по сигнатурам; setjmp/longjmp пока не заданы).
- Итог прогона XenonRecomp: 287 МБ C++ (~330 файлов) за ~6 с; "jump outside function" = 0.
- Конвейер: find_jumptables -> switch_function_bounds -> (запуск XenonRecomp) -> cap_overlapping_functions -> дописать в functions -> повтор.

## Недостающие инструкции (сделано)
- tools/xenonrecomp-mgrr.patch — патч к hedge-dev/XenonRecomp@ddd128b (`git apply`, файл XenonRecomp/recompiler.cpp):
  update-формы загрузок/сохранений (lbzux lhzu lhzux lhau lwzux ldux lfsu lfsux lfdu stbux sthu sthux stdux stfsu stfsux stfdu),
  eqv addc addme subfze fnmadd, bdnzt bdzf, vnor/vnor128, vcmpgtuw(.) vcmpgtsw(.), vaddsws, vctuxs/vcfpuxws128, vpkswss128, lvehx;
  vandc и vsel128 подключены к существующим обработчикам; счётчиковые переходы (bdz/bdnz/bdnzf/bdnzt/bdzf) за пределы функции -> хвостовой вызов.
- Результат: XenonRecomp без единого предупреждения; все 513 файлов сгенерированного C++ проходят `clang++ -fsyntax-only` (0 ошибок).
- НЕ проверено: семантика новых инструкций тестами (XenonTests не запускал), линковка и запуск.

## Что осталось
- Рантайм: ядро Xbox (xboxkrnl/XAM imports), память (гостевое адресное пространство 4 ГБ, big-endian), потоки (6 -> 3 ядра), GPU (Xenos -> Vulkan/NVN), XAudio2, XInput.
- setjmp/longjmp (адреса не найдены), ручной switch 8298C30C, midasm-хуки по необходимости.
- Размер: 287 МБ C++ / 513 файлов (-fsyntax-only ~2 мин на 8 потоках); оценить размер NRO и время полной сборки под aarch64.

## Пробная сборка под Switch (devkitA64, GCC, -O2, -mtune=cortex-a57) — сделана
- Код XenonRecomp заточен под clang: для GCC нужен tools/switch_build/gcc_compat.h (4 билтина). С ним все 513 файлов + ppc_func_mapping.cpp собираются без ошибок.
- Размер: код AArch64 ≈ 62 МБ (.text+rodata 65,1 МБ в Berkeley-формате), data 2,2 МБ; объекты 99 МБ; таблица функций (ppc_func_mapping.o) 12 МБ. Исходный PPC .text = 16,3 МБ (расширение ×4).
- Время: ≈ 2 670 CPU-секунд суммарно (~44 мин), максимум на файл 30 с и ~1,1 ГБ RAM; ~8 мин реального времени при 6 параллельных сборках. Итоговая линковка и NRO — не пробовал.
- Не определены после линковки только 201 импорт ядра/XAM (x360_work/runtime_imports.txt) + exp2f, log2f, sqrt. Остальное (save/rest-хелперы, все sub_*) самодостаточно.
- Импорты по группам: Xam 30, Nt 27, Ke 26, Rtl 22, Vd 21, Net 15, Ob 8, Io 7, Ex 6, Mm 6, Xe 6, XAudio 5, прочие 18.
- Важное для рантайма:
  * Графика: всего 21 Vd*-вызов (VdInitializeRingBuffer, VdSwap, VdSetGraphicsInterruptCallback …) — D3D-библиотека Xbox слинкована в exe, игра пишет PM4-команды в ring buffer. Нужно либо находить и подменять функции статически слинкованного D3D (как UnleashedRecomp), либо эмулировать командный процессор. Имён у функций нет (strip) — потребуется опознание по сигнатурам/строкам.
  * Звук: XAudioSubmitRenderDriverFrame / XAudioRegisterRenderDriverClient (CRI Atom кладёт PCM сам) + XMACreateContext/XMAReleaseContext (XMA-декодер нужен; Wwise/CRI на 360 используют XMA).
  * Сеть: 15 NetDll_* (сокеты), можно заглушки.
  * Прочее: ввод Xam Input*, контент/сейвы Xam Content*, потоки Nt/Ke/Ex, критические секции Rtl*, виртуальная память Nt/Mm.

## D3D (Xbox 360) — поиск функций (начато)
- Инструменты: tools/x360img.py (строки, xref'ы, bl-вызовы, .pdata), tools/asm.py (асм функции из комментариев сгенерированного C++).
- Каталог: x360_work/D3D_CATALOG.md, d3d_entrypoints.json (121 точка входа), d3d_setters.json (71 установщик состояния).
- Ядро D3D: 0x826AA000–0x826CC000; библиотека D3D слинкована в exe без символов. Указатель на устройство — глобал 0x82833A08.
- Игровой код зовёт ядро D3D почти целиком из тонкого слоя 0x8278DFC8–0x82796128 (обёртка движка над устройством).
- Опознано: Release (0x826AB228, атомарный dec), Swap (0x826B3510, VdSwap), init ring buffer (0x826B5A08),
  две функции с DRAW_INDX (0x826BC928, 0x826BCD40), sync/resolve (0x826AFE80), семейство установщиков состояния (0x826B62B0–0x826B8000).
- Ложный след: «PM4-константы» 0xC0xxxxxx в 0x8271xxxx — это отрицательные float; 0x826CC000 и 0x8271D000–0x8271E000 — CRT/хелперы, не D3D.

## Слой обёрток движка (0x8278E000–0x82796800) — разобран частично
- Это тонкие функции: «сравнить с кэшем состояния → вызвать 1–2 функции D3D-ядра».
- Файлы: x360_work/WRAPPERS.md (гипотезы и профили обёрток 360). Инструмент: tools/call_args.py (гистограммы аргументов в местах вызова).
- Подтверждено (high): SetPixelShaderConstantF = 0x82796620 (ядро 0x826AC190); SetVertexShaderConstantF = 0x827966A0 (ядро 0x826AC0B8).
- Вероятно: Clear = 0x82791750; SetStencilWriteMask = 0x82790AA0; создание текстур/целей = 0x82792740, 0x82795548.
- Не получилось: опознать булевы/enum-состояния (cull, z, blend…) по числу вызовов и аргументам — статистики недостаточно.
- Следующие способы: (а) декодировать семантику установщиков по функции отрисовки/flush (0x826BC928, 0x826BCD40) — какое слово теневого состояния в какой регистр Xenos уходит; (б) динамически: трассировка вызовов обёрток при работе игры под Xenia/в нашем рантайме и привязка к изменениям регистров GPU.

## Статическое чтение DrawVertices (0x826BC928) — установщики состояния расшифрованы
- rea для этой задачи не годится: raw-binary поддерживает только 6502/x86/x86_64/arm/arm64 (нет PowerPC). Использованы свои скрипты (tools/asm.py, tools/decode_setters.py).
- 0x826BC928 = D3DDevice DrawVertices(dev, primType=r4, start=r5, count=r6): сбрасывает 5 масок «грязных» регистров (dev+0, +8, +16, +24, +32) и пишет DRAW_INDX.
  Эмиттер серий регистров 0x826CA450: cntlzd по маске → заголовок ((n-1)<<16)|reg + n слов из теневой копии. Бит маски жёстко привязан к регистру Xenos.
- Группы dev+16: A bits0–11 → 0x2200.. (тень dev+10548); B bits12–16 → 0x2180.. (10528); C bits42–57 → 0x2000.. (10368); D bits21–41 → 0x2100.. (10444).
  dev+24: bits34–54 → 0x2280.. (10596); dev+32: bits38–45 → 0x2380.. (10832), bits0–37 → 0x2300.. (10680). dev+0 → вершинные float-константы 0x4000 (тень dev+1920),
  dev+8 → пиксельные 0x4400 (dev+6016), dev+32 bit56 → bool/loop 0x4900 (dev+10112). Остальное: 0x826CA7E8 (float-константы, 16 слов/бит), 0x826CA690 (fetch-константы 0x4800, 6 слов/бит).
- Итог: x360_work/RENDER_STATE_MAP.md — 74 установщика, 41 с D3D9-аналогом (RB_DEPTHCONTROL и RB_BLENDCONTROL_0 разобраны целиком по битовым полям).
- Обёртки движка, опознанные этим способом: CullMode 0x82791148 (80), ZEnable 0x827910E0 (33), ZWriteEnable 0x82791078 (32), ZFunc 0x82791010 (5), StencilEnable/TwoSided 0x82790768 (29),
  AlphaBlendEnable 0x82790ED8 (107), BlendFunc 0x827903E0 (24), BlendFuncSeparateAlpha 0x82790368 (6), SeparateAlphaBlend 0x82790E88 (11), ColorWriteMask RT0 0x82790D60 (112), StencilWriteMask 0x82790AA0 (10).
- Не разобрано: группа B (0x2180–0x2184, 16 установщиков с «ниблами»), fetch-константы текстур/вершин (0x4800), шейдеры, Resolve/EDRAM, PM4 DRAW_INDX-ветка, 0x826B6800/0x826B8E48/0x826AC3D8.

## Fetch-константы и шейдеры — разобрано (подробно в x360_work/FETCH_AND_SHADERS.md)
- SetTexture=0x826AEFD0, SetStreamSource=0x826B8670, SetIndices=0x826B8790, SetVertexShader=0x826AC618, SetPixelShader=0x826AC410, CreateShader=0x826ACC10/0x826ACDF8; таблица установщиков сэмплера (tools/decode_samplers.py).
- Новые инструменты: tools/cpk.py (CPK+CRILAYLA с затёртой сигнатурой), tools/pgdat.py (Platinum DAT), tools/shaders/ (конвейер).
- Шейдеры: 1 540 штук в shader.dat/shader2.dat/shadereff.dat (формат 0x102A1100/1101). XenosRecomp без правок: 1488/1492 → HLSL, 1482/1488 → SPIR-V (DXC). Не прошли 10 (0,7%).

## Форматы текстур и тайлинг — разобрано и проверено на картинках (x360_work/TEXTURES.md)
- Инструменты: tools/wtb.py (WTB + декодер fetch-константы), tools/xtex.py (endian, untile, DXT1/2_3/4_5, 8888, A8, mips, PNG), tools/scan_textures.py (индекс). Нужен venv с numpy/pillow (scratchpad/venv).
- В файлах игры 5 049 текстур: DXT1 2657, DXT4_5 2261, 8_8_8_8 67, DXT2_3 60, A8 4; 2D и 4 куба; всё влезает в BC1/BC2/BC3 + RGBA8 + R8.
- texinfo в WTB = D3DBaseTexture (52 байта); fetch-константа +28…+48 — то, что копирует SetTexture.
- Mips: уровни 1…n−1 идут подряд с mip_addr; хвост (≤16 блоков) упакован в один тайл — смещения внутри тайла пока не декодированы (план: достроить на GPU).

## Вершинные форматы — разобраны и проверены на реальных вершинах (x360_work/VERTEX_FORMATS.md)
- Формат вершин задаётся деклараций D3D, а не буфером: CreateVertexDeclaration=0x826AC938 (элемент 12 байт: stream, offset, type u32, method, usage, index), SetVertexDeclaration=0x826AC830 ([dev+12240], бит 19 в dev+16).
- 74 декларации в образе 360 (x360_vertex_decls.json).
- Слово типа Xbox: биты 0–5 формат Xenos, 6–7 endian, 8 signed, 9 integer. 10 типов: FLOAT1/2/3/4, FLOAT16_2/4, UBYTE4(N), D3DCOLOR и **10_11_11 (нормали, только Xbox)**.
- Нормаль 10_11_11 расшифрована на em0200.wmb: из BE-dword поля 10/11/11 от старших бит, длина 0,999±0,000 на 2000 вершинах; тангент — ubyte4n [W,X,Y,Z]; индексы/веса костей — естественный порядок байт.
- ИСПРАВЛЕНИЕ по текстурам: ранее учитывал только .wtb. Текстуры моделей лежат в .wta (заголовки, в .dat) + .wtp (данные, в .dtt). Итого 8 303 текстуры, 1,9 ГБ; набор форматов не изменился.
- Инструменты: tools/vdecl.py, tools/scan_textures.py (теперь с .wta).

## DRAW_INDX и Resolve — разобрано (x360_work/DRAW_AND_RESOLVE.md)
- DrawVertices=0x826BC928 (пакет 0xC0012201, auto-index), DrawIndexedVertices=0x826BCD40 (0xC0032201: viz, initiator, DMA_BASE, DMA_SIZE), VGT_INDX_OFFSET 0x2102; primType = значения Xenos (1,2,3,4,5,6,8=RECT_LIST,13=QUAD_LIST), таблица 0x8218C968, порции по 65535.
- SetRenderTarget=0x826B8ED8, SetDepthStencilSurface=0x826B9268, создание поверхностей 0x826AED70; форматы движка: A8R8G8B8 0x18280186, R32F 0x2DA2ABA4, 0x1A215586 (неопознан), глубина D24FS8 0x1A220197; MSAA 2×/4×.
- Resolve=0x826B9F78 (обёртка 0x826BB0D0): Flags бит0–2 источник (4=depth), бит4–6 фрагмент, бит14/15 очистка; движок делает ручной ресолв глубины по сэмплам (0x8278E0A0).
- Для порта Resolve = копия/блит поверхности в текстуру (+очистка), эмуляция EDRAM не нужна.

## Формат моделей WMB4 — разобран и проверен на 945 моделях (x360_work/WMB4_FORMAT.md)
- Инструменты: tools/wmb.py (парсер), wmb_preview.py (рендер в PNG), wmb_validate.py (проверка инвариантов), extract_wmb.py (выгрузка 946 моделей в x360_work/wmb/).
- Таблицы: группы вершин (28 Б), батчи (20 Б, префиксные суммы), описание батчей (16 Б), кости (32 Б, id/parent/local/world), таблица трансляции костей (3 уровня по ниблам, смещения в u16), наборы костей (u8), материалы (24 Б: ключ шейдера, привязки (роль, индекс текстуры), float-параметры), текстуры (id = id в WTA), части (68 Б: имя, бокс, списки батчей и материалов).
- Индексы u16 BE, локальные для батча, TRIANGLE_STRIP с рестартом 0xFFFF. Вершина: POS f32x3 @0, UV f16x2 @12, NORMAL 10_11_11 @16, TANGENT u8x4n [W,X,Y,Z] @20, далее BLENDINDICES/BLENDWEIGHT (флаг 0x30) или COLOR/UV1; шаг и доп. поток определяются флагами hdr[2].
- Валидация: 792 модели проходят все проверки, 9 пустых, 144 (ба-окружение) без полного описания батчей; всего 4,52 млн вершин и 6,93 млн треугольников.

## Просмотрщик моделей (tools/viewer.py) — работает; уточнения формата WMB4 из этапа просмотрщика
- tools/viewer.py (moderngl/OpenGL, офскрин в PNG или окно --window), tools/model_loader.py (WMB+WTA из .dat, WTP из .dtt, общие текстуры флаг 0x63 из core.dtt::pre_texture.wtb). Запуск: `venv/bin/python tools/viewer.py pl0100 --gain 2.5`. Нужен venv с numpy, pillow, moderngl (glfw для окна).
- Результат: pl0100 (Райден), em0010 (солдат), em0200 (враг с лезвиями) отрисованы с текстурами и освещением; текстуры и UV сопоставлены верно.
- ИСПРАВЛЕНИЯ прежних выводов (ошибки найдены именно просмотрщиком): (1) нормаль 10_11_11: X = младшие 11 бит, Y = средние 11, Z = старшие 10 (не X сверху); (2) тангент в памяти [W,Z,Y,X];
  (3) тип примитива по hdr[3] бит 16: ленты с рестартом, иначе списки (раньше везде считал лентами); (4) шаг потоков — по флагам, а не по смещениям (выравнивание до 16 Б); «шаг 26» — это выравнивание;
  (5) материал батча = старшие 16 бит третьего слова описания батча; список материалов части — набор, а не параллельный список; (6) число привязок материала = второй u16, пара (0,0) — реальная привязка;
  (7) батчи вне списков частей — лишняя геометрия (не рисовать); состояния частей (nocut_/cut-/dam/blood) показываются игрой выборочно; (8) обход вершин — по часовой стрелке (D3D).
- Проверка нормалей/обхода: на 156 моделях медианная доля согласованных треугольников 100%.
- Текстуры моделей окружения (ba/bh/bm…) лежат не в их файлах: недостающие id ищутся в глобальном индексе x360_work/textures_index_all.json (model_loader._load_indexed), общие (флаг 0x63) — в core.dtt::pre_texture.wtb. Итоговая галерея: x360_work/viewer_gallery.png (Райден, солдат, враг, пальма, оружие). Описание: tools/VIEWER.md.

## Настоящие шейдеры (REAL_SHADERS.md)
Модели WMB рисуются оригинальными шейдерами Xenos через XenosRecomp→SPIR-V в C++ Vulkan-рендерере (`tools/vkview`, `tools/export_scene.py`, `tools/shaders/shaderlib.py`). 85/90 проверенных моделей рисуются, em0200/em0100/pl0100 и props выглядят корректно. Найдено: тангент UBYTE4N (spec bit0=0, float-биты в uint4), цвет вершин ARGB, fogParam c184, правило выбора VS. Открыто: освещение/тени/фог, альфа по материалам, кости/анимации, 10 неконвертируемых шейдеров, cube-текстуры.

## S0 — микробенчмарк на Switch (tools/s0bench)
96 детерминированных функций-листьев игры (из 2331 кандидатов; 780 падают и 12 недетерминированы на синтетических входах). Счёт инструкций совпадает при -O0 и -O2. PC i3-12100F: медиана 5639 гостевых MIPS. Собран `out/sd/switch/s0bench/s0bench.nro` (+ проба методов гостевой памяти A/A2/B/C/E); ждёт запуска на Erista (штатная частота и 1785 МГц).
Попутно: XenonRecomp генерирует обращения к памяти через `volatile` — мешает оптимизации, кандидат на M4. Все 2331 функции одним файлом при clang -O1 не собрались за 20 минут (причина не выяснена); после разделения на 16 файлов сборка занимает секунды.
Первый запуск на Erista (1020 МГц, Application, 3,19 ГБ): гостевая память не выделилась. svcMapPhysicalMemory → 0xFA01 (у hbl SystemResourceSize = 0), svcMapMemory в alias → 0xDC01, stack-регион 2 ГБ мал. Вывод: разреженного окна 4 ГБ у homebrew через hbl нет. Решение (способ F, проверяется): окно внутри обычной кучи, все гостевые аллокации HLE-ядра кладём по низким адресам, промежуток до образа отдаём под malloc порта.
Второй запуск (способ F) прошёл 68 функций и упал на f_82407C98: чтение гостевого адреса 0x0401C000 (за пределами гостевой области). Причина — гостевая память в куче hbl не обнулена, а на PC была нулевой; функции читают по указателям туда и идут другим путём. Проверено: aarch64 GCC 14 (Linux arm64 через qemu) даёт те же счётчики, что x86 clang, для всех 288 запусков. Исправление: memset гостевой области. Функции, у которых компилятор свернул цикл (>50 000 MIPS), исключаются из статистики.
Предварительно (68 функций, с оговоркой про мусор в памяти): Switch @1020 МГц медиана ~330 гостевых MIPS, отношение к i3-12100F — медиана 0,086 (~1/11,6).
S0 полный прогон на Erista (clkrst сообщил 1020 МГц, пользователь выставлял 1785 в sys-clk — частота под вопросом): 96/96 функций без падений; медиана 820 гостевых MIPS; отношение к i3-12100F по 81 скалярной функции — медиана 0,134 (векторные 1,18 — это копирования памяти, артефакт синтетики). Добавлен замер реальной частоты цепочкой зависимых ADD.
S0 итог (Erista, 81 скалярная функция, без свёрнутых): 1020 МГц — медиана 612 гостевых MIPS (0,60 на МГц), 1785 МГц — 901 (p10 241, p90 1928); масштабирование с частотой 1,72 при отношении частот 1,75 → упор в CPU, не в память. Относительно i3-12100F: 0,082 / 0,135. clkrst показывает частоту верно (совпадает с замером цепочкой ADD); в прошлом прогоне «1020 МГц» был файл от 1785 (медиана 820 = 818).
Важно: x360_work/mgrr.toml собран без оптимизаций XenonRecomp (skip_lr, ctr/xer/reserved/cr/non_argument/non_volatile_as_local) — регистры живут в структуре ctx в памяти. UnleashedRecomp включает их все. Следующий эксперимент: S0b — те же функции с этими опциями.
S0b: x360_work/mgrr_opt.toml = mgrr.toml + skip_lr, skip_msr, ctr/xer/reserved/cr/non_argument/non_volatile_as_local → x360_work/ppc_opt (6 с, 0 предупреждений). Варианты бенчмарка: base, opt, optnv (opt + доступ к памяти без volatile, -fno-strict-aliasing). Счётчики и r3 всех 288 запусков совпадают между вариантами на x86 clang и aarch64 GCC. PC i3-12100F, медиана: base 5254, opt 6675 (+27%), optnv 8437 (+61%) гостевых MIPS. .nro с тремя вариантами ждёт прогона на Erista.
S0b на Erista (79 скалярных функций, не свёрнутых ни в одном варианте), медиана гостевых MIPS: 1020 МГц — base 611, opt 807, optnv 898; 1785 МГц — base 901, opt 1240, optnv 1550. Ускорение optnv/base на функцию: медиана 1,53, geomean 1,57 (p10 1,00, p90 3,54). Векторные (копирования): aggregate 171 → 401. Вывод: в порт идёт вариант optnv (опции *_as_local + доступ к памяти без volatile, -fno-strict-aliasing); volatile нужен только для MMIO — отдельным путём.
Поиск оптимизаций: x360_work/OPTIMIZATIONS.md. Найден UnleashedRecomp-NX (Switch-порт Sonic Unleashed, GPL-3): разреженное окно 4 ГБ под hbl через svcMapProcessCodeMemory+svcMapProcessMemory; Vulkan через свою сборку Mesa NVK; 30 кадров/с при ~400p.
Изучены похожие проекты (x360_work/PRIOR_ART.md): xbox360-nx/rexglue-sdk-nx (NFS MW 32–35 кадров/с на штатных частотах, deko3d, PM4-кольцо, LTO/PGO, прямые вызовы), UnleashedRecomp-NX, LostOdysseyRecomp. MGR, как и NFS, хранит в WTA/WTB готовые D3DBaseTexture → перехват CreateTexture недостаточен.
S0c собран: варианты g2 (GCC -O2), g3 (GCC -O3), g2nf (GCC -O2 без переключений flush-to-zero), c2/c3 (clang 22 -O2/-O3, заголовки devkitA64) — все на кодогене ppc_opt без volatile; память — способ G (svcMapProcessCodeMemory + svcMapProcessMemory, окно 4 ГБ из ASLR), запасной F. Счётчики и r3 всех 288 запусков совпадают с optnv на x86 и aarch64 (GCC 14 и clang 19). PC i3-12100F, медиана: g2 8953, g3 9504, g2nf 9134, c2 9802, c3 9805. LTO на листовых функциях не проверяется (нет вызовов) — перенесён в M1.
S0c на Erista: память способом G работает (syscall-подсказки есть, все вызовы rc=0, окно 4 ГБ из ASLR). Скалярные функции (80 шт., без свёрнутых), отношение к g2 (GCC -O2) по функциям: 1020 МГц — g3 медиана 1,00 / geomean 1,15; g2nf 1,00/1,00; c2 1,23/1,42; c3 1,25/1,44; суммарное время: clang быстрее в 1,70 раза. 1785 МГц — c2 1,19/1,40, суммарно 1,57. Медиана g2 → c2 при 1785: 1432 → 1772 MIPS. Вывод: clang для кода игры — да (оговорка: PGO у clang под aarch64-none-elf без compiler-rt недоступен — сравнить с GCC+LTO+PGO в M1); -O3 не нужен; переключения flush-to-zero на этих функциях ничего не стоят — оставляем как есть (точность).
M0.1: git-репозиторий (whitelist .gitignore), LICENSE GPL-3, README, подмодуль sdk = rexglue-sdk-nx@f961303 (ветка switch). Покрытие импортов MGR в SDK: 201/201 (185 REX_EXPORT + 4 REX_HOOK_RAW: DbgPrint, sprintf, _snprintf, _vsnprintf; 12 заглушек: Io*ShareAccess, IoCompleteRequest, IoDeleteDevice, IoInvalidDeviceRequest, NtCancelIoFile, NtWriteFileGather, ObIsTitleObject, ObReferenceObject, RtlUpcaseUnicodeChar, XamShowMarketplaceUI). Схема игры в xbox360-nx: <game>_manifest.toml + overrides.toml (функции, share_registers, опции регистров) + huecos.toml (дыры между функциями).

## M0.2 — кодоген ReXGlue на MGR
- Кодоген SDK собран на Linux (`cmake --preset linux-amd64 -B build/sdk-host -DREXGLUE_ENABLE_TRACY=OFF`, цель `rexglue`, ~2 мин; бинарник `sdk/out/linux-amd64/Release/rexglue`). Подмодули SDK: `git submodule update --init --depth 1 --recursive`.
- Проект игры: `game/mgrr_manifest.toml` (+ `analysis_functions.toml`, `analysis_switch_tables.toml` от `tools/to_rexglue.py`, ручной `overrides.toml`). Вывод — `game/generated/` (в git не попадает). Один прогон ~6 мин.
- Без подсказок анализатор ReXGlue дал 6 ошибок (5 thunk-функций 0x8266A500..A560 и обрезанная 0x824E9B60). С нашими 596 таблицами переходов, стартами функций и overrides: 0 ошибок, 0 неразрешённых переходов, 541 файл (282 МБ), 85 826 функций.
- Сравнение с XenonRecomp (130 715 функций): 44 818 лишних у XenonRecomp — нулевые заполнители; 492 — код, которого анализатор не видит (в основном thunk'и, доступные только через vtable): 472 объявлены (`x360_work/rexglue_extra_functions.txt`), 20 лежат внутри функций ReXGlue и не объявлены (`x360_work/rexglue_inner_entries.txt`, проверить в M1 по «call to unregistered function»).
- Вложенные границы из нашего списка ReXGlue не принимает: объявляем только старты; 0x82ADBC0C — метка внутри 0x82ADBA90 (исключена, конец 0x82ADBA90 задан вручную).
- Опции регистров (`*_as_local`) пока выключены: в M1 включать по одной с проверкой (у xbox360-nx ломались `skip_lr` и `non_argument_as_local`).

## M0.3 — сборка и первый запуск на Linux
- `game/CMakeLists.txt` + `src/main.cpp`, `src/mgrr_app.h` по шаблону `rexglue init`; SDK подключается `-DREXSDK_DIR=<repo>/sdk`. Флаг `-march=x86-64-v3` обязателен (без SSSE3 SDK не компилируется; пресеты SDK ставят v2/v3).
- Сборка clang 22, Release, `-j5` (15 ГБ RAM): SDK + 267 файлов кода игры за 12 мин, 0 ошибок; `build/linux/mgrr` 168 МБ. Скрипты: `tools/build_from_iso.sh` (распаковка `tools/xdvdfs.py` — побайтно совпадает с x360_extracted, проверка sha256 default.xex, кодоген, сборка), `tools/run_linux.sh` (запуск с `--gpu_plugin=xenos --mnk_mode=true` — клавиатура как геймпад, по умолчанию в SDK выключена; без LD_LIBRARY_PATH не находится librexruntime.so).
- Без GPU-плагина игра доходит до `VdInitializeRingBuffer` и ждёт. С `--gpu_plugin=xenos` (Xenia, Vulkan, RTX 3050) за 60 с: без падений и «unregistered function», ~90 МБ прочитано из cpk, звук идёт (XMA), создаётся ~40 пайплайнов, swapchain 1920×1080; единственное массовое предупреждение — ~1800 «Texture fetch constant has invalid type» (нулевые fetch-константы). Что на экране — проверить глазами.
- Проверено глазами: стартовый экран «Press Start», без артефактов, звук нормальный.

## M0.4 — сборка под Switch
- `python3 sdk/tools/switch_setup.py` (подмодули для Switch, SPIRV-Cross и uam на фиксированных коммитах, конфиг FFmpeg поверх подмодуля).
- Тулчейн `tools/switch_build/switch-devkitA64.cmake` — из xbox360-nx без изменений (`-D_GNU_SOURCE`, Tracy выкл., TLS local-exec, `--no-relax`). Со стандартным `Switch.cmake` devkitPro SDK не собирается (newlib прячет strdup/fileno/posix_memalign, Tracy не знает Horizon).
- Конфигурация в контейнере devkitpro/devkita64: `cmake -S game -B build/switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/switch_build/switch-devkitA64.cmake -DCMAKE_BUILD_TYPE=Release -DREXSDK_DIR=sdk -DREXGLUE_SWITCH_DEKO3D=ON`. GCC 15.2 -O3 на всём (clang для кода игры — следующим шагом).
- `mgrr_register.cpp` (одна функция, ~86 тыс. вызовов SetFunction) GCC -O3 не собрал за 30+ мин → для него `-O1` (game/CMakeLists.txt); пересборка 55 с.
- Итог: 0 ошибок, `build/switch/mgrr.nro` 78 МБ. Без `--game_data_root` данные ищутся в `game/` рядом с NRO (`sdmc:/switch/mgrr/game/`). GPU-плагина на Switch нет — ожидаем остановку на `VdInitializeRingBuffer`, как на PC без плагина.
- **Первый запуск на Erista** (handheld, CPU 1020 / GPU 460,8 / память 1600 МГц, 3 ядра, маска 0x7): гостевая память отображена SDK сама, SD смонтирована (57 записей за 75 мс), за ~1,3 с код игры доходит до `VdSetGraphicsInterruptCallback` → `VdInitializeRingBuffer` → `VdEnableRingBufferRPtrWriteBack` (игнорируются: нет GPU) и встаёт. Без падений; гость 367 МБ; поток «Main XThread» крутится на 100 % одного ядра (ожидание GPU), остальные потоки игры спят. Логи: `out/sd/switch/mgrr_logs/` (mgrr_001.log, rex/rex_perfil.log — профиль SDK каждые 10 с).

## S1 (начало) — счётчик draw на PC
- `game/src/mgrr_draw_stats.cpp`: хуки DrawVertices 0x826BC928, DrawIndexedVertices 0x826BCD40, Resolve 0x826BB0D0, Swap 0x826B3510; `--mgrr_draw_stats=true` (включён в tools/run_linux.sh), окно `--mgrr_draw_stats_seconds` (10 с). Лог: строки `[draws]` в build/run.log.
- `mgrr_register.cpp` собирается без PCH (clang отвергает PCH с другим -O).
- Первый замер (PC, без ввода, 70 с): логотипы — 60 Swap/с, 6 draw/кадр, 10 resolve/кадр; стартовый экран с 3D-сценой — **30 Swap/с**, draw/кадр среднее 395–561, p95 до 743, максимум 747, ~440 тыс. примитивов/кадр, 20 resolve/кадр, 46–67 % draw индексные. Откуда 30 (ограничение игры или упор эмуляции на PC) — выяснить.
- Почему 30 на стартовом экране: главный поток 39/40 срезов стека (gdb) крутится в `sub_8278E640` ← кадровая функция 0x82791828; она опрашивает счётчики устройства D3D `dev+16968`/`dev+16976` через 0x826B2FC0 (ожидание vblank/завершения кадра). С `--vsync=false` (vblank SDK 1000 Гц) та же сцена идёт ~120 кадров/с (~410 draw, 440 тыс. примитивов). Значит, 30 — собственный лимит игры для этой сцены, а не предел PC. Это ожидание — кандидат на хук «реальное ожидание вместо опроса» на Switch.
- Ролики: 39 файлов `media/movie/*.usm` (CRI Sofdec), видео MPEG-2 1024×576 29,97 кадра/с — декодирует библиотека CRI в коде игры на гостевом CPU (поток «CriMana…» на PC ~7 % ядра). На Switch проверить стоимость; в SDK есть FFmpeg для Switch (нативная замена возможна).

## M1 (Switch) — графическая система без отрисовки
- `game/src/mgrr_gpu.cpp/.h` — из xbox360-nx `halo3/src/halo3_gpu.cpp` (GPL-3, только переименование; код не зависит от игры): MMIO GPU 0x7FC80000, поток чтения PM4-кольца (rptr writeback, WAIT_REG_MEM с отсечкой 200 мс, MEM_WRITE/COND_WRITE/EVENT_WRITE*/scratch-регистры, occlusion = 1000), прерывания vblank (60 Гц) и PM4_INTERRUPT, presenter deko3d (чёрный экран + оверлей), Resolve/clear передаются в RenderDk. Подключается только в Switch-сборке (`MGRR_DEKO3D`, `OnPreSetup`). На Switch `mgrr_draw_stats` включён по умолчанию.
- Журнал Halo 3 (xbox360-nx `halo3/PORTING.md`) — что проверить в MGR: (1) таблицы переходов, которые ReXGlue угадал одноэлементными — у нас 615 switch, минимум 10 case, ни одного одноэлементного; (2) барьеры памяти: `sync/lwsync/eieio` → `std::atomic_thread_fence` (489 мест; в нашем SDK уже исправлено); (3) XAPI-волокна (SwitchToFiber) — признаков в MGR не найдено; (4) busy-wait'ы рабочих потоков (Halo: поток 98 % на опросе очереди) — смотреть в rex_perfil.log со срезами стека (`logs/rex/perfil_pilas.flag`).
- Вариант clang для A/B на консоли: `tools/switch_build/clang_recomp.sh` (заголовки devkitA64 → `build/dka64-sysroot`, копия из контейнера): хост-clang 22 `--target=aarch64-none-elf -mcpu=cortex-a57 -O2 -fno-strict-aliasing -ffp-contract=off -fno-math-errno` собирает 268 файлов кода игры (~15 мин, -j5), затем линкует командой GCC-сборки с подменой объектов → `build/switch/mgrr_clang.nro` (69 МБ против 81 МБ). `mgrr_register.cpp` остаётся от GCC (clang -O1 не собрал его за 15 мин). Без `-fno-math-errno` clang вызывал `fma`/`sqrtf` из libm вместо инструкций. Объект mgrr_recomp.10: те же 247 функций, код на 15 % меньше, TLS-релокаций нет.
- **Первый запуск на консоли (21.10):** обе сборки (GCC и clang) через ~1,5 с после старта колец падают одинаково. Сначала поток игры загружает `data000.cpk`, затем идёт серия `AllocFixed: no se pudo confirmar 0x1000 bytes … Fallo 0x0000CE01; respaldo 448 MB, mapeado 528 MB`. 0xCE01 = ядро, 103 (ResourceExhausted): кончились блоки памяти ядра, а не сама память — title takeover, лимит процесса ~3,2 ГБ. Отчёт Atmosphère (clang) — тот же результат 0xCE01. GCC вместо fatal повесил всю консоль (ресурсы ядра общие).
  - Причина: PhysicalHeap MGR выделяет по 4 КБ (адреса идут вниз от 0x13E7xxxx), и `memory_switch.cpp` `AllocFixed` делал на каждую страницу отдельный кусок со своим отображением ядра. Путь через исключение (exception_handler_switch.cpp, шаг 3) уже коммитил кусками по 4 МБ именно из-за этого лимита, а явный коммит — нет.
  - Правка SDK (`tools/sdk_patches/0001-…patch`; `build_from_iso.sh` накладывает её на подмодуль, указатель подмодуля не меняется): `AllocFixed` коммитит физическую память гостя (смещения 0x1_0000_0000–0x1_2000_0000) целыми выровненными кусками по 4 МБ, как путь через исключение. Права по-прежнему ставятся только на запрошенный диапазон.
- **Второй запуск (21.10, с правкой SDK):** обе сборки работают без падений, звук есть, на кнопки реагируют, экран чёрный. Сборку clang вслепую довели до геймплея. Гость занимает 474–476 МБ при 710–732 МБ отображений; процесс ~3,2 ГБ (title takeover). Erista: CPU 1020, GPU 460,8 МГц; процессу доступны 3 ядра, то есть 300 %.

  | Сцена | GCC | clang | draw/кадр | CPU |
  |---|---|---|---|---|
  | Логотипы | 60 к/с | 60 к/с | 6 | 113 % |
  | Стартовый экран (у игры лимит 30) | 20–20,5 к/с | 21,5–22,2 к/с | 390–560, ~440 тыс. примитивов | 243–247 % |
  | Геймплей (только clang) | — | 37–48 к/с | 280–385, ~500–540 тыс. примитивов, 90 % индексные | 190–203 % |

  - Стартовый экран упирается в CPU: занято ~82 % от 300 %. Больше всего грузят три потока, созданные для этой сцены (34–42 % каждый), затем 4 рабочих потока движка (12–20 %), Main XThread (18 %) и Wwise EventManager (10–12 %). Clang быстрее GCC примерно на 8 %.
  - На геймплее CPU не упирается (~200 из 300 %), ни один поток не занят на 100 %, а кадров 37–48 — значит, кадр ждёт чего-то последовательно. Нужны срезы стеков (`logs/rex/perfil_pilas.flag`).
  - Важно: это без отрисовки. GPU-кольцо читается (2,7–4,4 % CPU), но draw не транслируются; затраты рендерера (8–19 мкс на draw на CPU в NFS) ещё впереди.
  - deko3d: один раз в логе «formato de destino de render todavia no soportado (causa 114)»; разобрать в M2.
- **Срезы стеков (clang, `perfil_pilas.flag`).** Адреса переведены в символы по ELF `build/switch/mgrr_clang` скриптами из scratchpad: `nm` + bisect, сумма долей срезов по потокам.
  - Стартовый экран (22 к/с, CPU 250 %). Потоки игры большую часть времени стоят в `rmutexLock` (`svcArbitrateLock`), а не считают. Заблокированные срезы в сумме ~176 % потоко-времени, и всё это в `sub_82791B20` — lock-free bump-аллокаторе (`lwarx`/`stwcx.` по курсору +16, выравнивание 32). Рабочий код (`sub_826A1168`, 20 %) и `pthread_mutex_unlock` (19 %) — следующие по весу.
  - Геймплей (~41 к/с, CPU 200 %). Та же картина, мьютекс ~150 %: `sub_82794780` 56, `sub_82791B20` 36, `sub_82BF6EF8` 29, `sub_82B91788` 18…
  - Причина — генератор. `mfmsr`/`mtmsrd r13` (на 360: «прерывания выкл.» вокруг `lwarx`/`stwcx.`) превращаются в `REX_CHECK/ENTER/LEAVE_GLOBAL_LOCK`, то есть в общий рекурсивный мьютекс SDK `global_critical_region`. Через него же работают xmemory/MMIO. За один проход мьютекс берётся 2 раза, и на Horizon каждый раз при конкуренции идёт svc.
  - Проверка: во всех 542 таких участках MGR (153 функции) только одна атомарная операция: 313 `lwarx/addi/stwcx.`, 113 CAS, 63 CAS 64-бит, 43 `add`, 8 обмен, 1 `and`. Других записей и вызовов нет. Сам `stwcx.` — это CAS хоста, так что блокировка ничего не добавляет. Решение: `skip_msr = true` в манифесте (как у UnleashedRecomp). После перегенерации `REX_ENTER_GLOBAL_LOCK` встречается 0 раз; изменено 203 файла из 541.

## M2 (спайк) — шейдеры MGR до DKSH на PC
Проверка главного риска M2 из PLAN.md: проходят ли шейдеры MGR путь, которым их компилирует консоль.
- Транслятор — форк XenosRecomp из xbox360-nx (`shaders/XenosRecomp`, правки `NFSMW_RECOMP`, свой `shader_common.h`, драйвер `nfsmw_hlsl.cpp`), собран g++ на хосте. Входы — 1492 контейнера из `shader.dat`/`shader2.dat`/`shadereff.dat` (`x360_work/shaders/all`, формат 2008 года `102A11xx`, как у Sonic; конвертер формата 2005 из NFS не нужен). Файлы для драйвера переименованы в `v_*.bin`/`p_*.bin`.
- Результат по цепочке:
  - HLSL: 1488 из 1492. Не прошли 4 VS частиц `espshadermulti*particle*` — «exportacion de vertices sin interpolador declarado» (memexport).
  - SPIR-V (DXC, флаги NFS для deko3d: `-T vs/ps_6_6 -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Werror=parameter-usage -DNFSMW_DEKO3D`, без `-fvk-invert-y`): 1478 из 1488. Не прошли 10: сэмплеры, не объявленные в таблице констант (`s1_*` ×4, `g_Sampler2_TextureCube*` ×3, `s16_*` ×2 — вершинная выборка текстуры), и 1 `getWeights2D`.
  - GLSL → DKSH: новый `tools/shaders/dksh_probe` (SDK `rex::deko3d::SpirvAGlsl` + uam из `sdk/thirdparty/uam`, как поток компиляции на консоли): **1478 из 1478 скомпилированы, без предупреждений**. uam тратит 15,7 с на все (в среднем 10,6 мс на PC; на консоли около 95 мс на шейдер по данным NFS, значит нужен офлайн-кэш DKSH, в SDK он есть: `cache_dksh.h`).
- Что ещё не проверено: компиляция не означает правильную картинку. Дальше проверять ABI с рендерером SDK: `EntradaShader` (элементы вершин, сэмплеры, маски выходов — их у NFS заполняет упаковщик библиотеки) и опознание VS. Как и у NFS, D3D патчит микрокод VS под формат вершин, поэтому VS ищутся через хуки создания шейдеров.
- Скрипты прогона (перевод, DXC) пока лежат во временной папке; перенести в `tools/shaders/` следующим шагом.

## M2 — библиотека шейдеров MGR
- Одна команда: `DXC=… tools/shaders/build_library.sh` (≈30 с). Шаги: `data000.cpk` → 1492 контейнера → HLSL (`tools/shaders/mgrr_hlsl.cpp` + `tools/shaders/xenosrecomp/`, копия форка xbox360-nx без изменений) → SPIR-V (DXC, режим deko3d) → `build/shaders/mgrr_shaders.mgsl` (15,7 МБ, наш формат: `game/src/mgrr_shader_library.*`) → проверка тем же кодом, что в игре, → DKSH через `dksh_probe`.
- `mgrr_hlsl` дописывает объявление для регистров сэмплеров, которых нет в таблице констант (`sN`). Так прошли `filtershadergather`, `filtershadergathernoise` (обе на стартовом экране), `filter10` и `filtershaderhdao`. Итог: 1482 из 1492 в библиотеке, все 1482 компилируются в DKSH. Не прошли 10: 4 VS частиц с memexport (HLSL) и 6 на DXC — `effectwaterdraw`, `espshaderbump`, `espshaderwaterwave` (VS и PS; там же вершинная выборка `s16`), `modelshadernewshadowreceiverspot`, `modelshadervelocityweight` (cube-выборка из регистра, объявленного как 2D, и `getWeights2D`).
- `game/src/mgrr_shaders.*` превращает контейнеры формата 2008 года в `rex::x360::EntradaShader` для рендерера SDK. Заполняются микрокод с маской патчей вершинных выборок, элементы вершин, выходы PS, сэмплеры по CTAB, объём констант и kills. Опознание: по контейнеру (`FindContainer`) и по микрокоду (`Identify`).
- Проверка на реальном микрокоде (кэш шейдеров Xenia после запуска до стартового экрана, `--xsh`): PS **88 из 88** опознаны по микрокоду, VS только 11 из 71 (D3D их патчит, как у NFS). 5 микрокодов встречаются в нескольких контейнерах с разным SPIR-V (частицы и эффекты shadereff) — разобрать, когда дойдёт до них.
- VS опознаются через хук `CreateXShader` (`game/src/mgrr_shader_hooks.cpp`, `0x826ACC10`/`0x826ACDF8`; `r3` = контейнер). Запуск на PC до стартового экрана: игра при загрузке создаёт около 1000 шейдеров, в библиотеке найдены все, кроме тех самых непереведённых (совпадают размеры: 1696 `modelshadernewshadowreceiverspot`, 824, 600, 632, 856, 692, 844 — шейдеры shadereff). Оба хука принимают оба типа; тип определяется по сигнатуре.
- Этот же запуск — первый с `skip_msr` на PC: 75 с без ошибок в логе, стартовый экран 30 к/с (лимит игры), демо-геймплей 60 к/с при 360–400 draw.

## M2 — декодер кольца (первая версия, на консоли ещё не проверена)
- `game/src/mgrr_gpu.cpp`: `DRAW_INDX`/`DRAW_INDX_2` пишут инициатор и `VGT_DMA_BASE/SIZE` в зеркало регистров (раньше не писали; у `DRAW_INDX` первое слово — запрос видимости). Copy-mode → `Copiar`, остальное → `RenderDk::Dibujar` с `PeticionDibujo`. Поколения констант VS/PS, fetch и viewport растут только при реальном изменении значения, как у NFS. `WAIT_REG_MEM` увеличивает `g_sincronizaciones_anillo`. Swap передаёт `LeerTexturaSwap` (fetch-константу 0) и размер из пакета — раньше была пустая.
- `IM_LOAD` / `IM_LOAD_IMMEDIATE`: PS опознаётся по микрокоду; VS — по адресу копии микрокода. Её записывает хук `CreateXShader` (`obj+24` после возврата, физический адрес; `mgrr_shader_hooks.h` `ShaderAtAddress`), запасной путь — по микрокоду. Пропатченный микрокод VS уходит в `vs_microcodigo`.
- Отчёт `[gpu]` раз в 10 с: как найдены шейдеры (VS по адресу, по микрокоду, не найдены; PS не найдены), сколько draw отправлено, а также `[gpu] renderer:` — нарисовано, отклонено с причинами (`EstadisticasDibujos::causas`), pipelines, текстуры.
- Ожидаемые ограничения рендерера SDK: цели цвета только 8_8_8_8 (причины 200+формат), без немедленных индексов (2) и без части примитивов (100+тип). У MGR будет видно по причинам отказов.
- Экранный оверлей (`game/src/mgrr_overlay.*`, `mgrr_stats.h`): справа вверху FPS, время кадра (среднее и максимум за секунду), draw и resolve за кадр (из хуков D3D, на обеих платформах). На Switch добавлены строки декодера: нарисовано, отклонено и без шейдера за секунду, главные причины отказов, как найдены VS и PS. `--mgrr_overlay` по умолчанию включён на Switch и выключен на PC; оверлей SDK остаётся на L+R+вверх. Проверено на PC: стартовый экран — 30,0 FPS, 33,3 мс, около 423 draw. Счётчики хуков draw теперь работают всегда; `--mgrr_draw_stats` включает только строку `[draws]` в логе.

## nxlink (2026-10-21)

`tools/nxlink.sh [IP] [-- аргументы]`: отправляет `out/sd/switch/mgrr/mgrr_clang.nro` в `sdmc:/switch/mgrr/` через netloader hbmenu (Y) и запускает его; `nxlink -s` принимает stdout, лог пишется и в `build/nxlink_logs/`. `nxlink` берётся из образа devkitpro/devkita64 (`build/tools/nxlink`). Игра (`game/src/mgrr_nxlink.cpp`, `OnPostInitLogging`) открывает сокеты только при запуске через nxlink (`__nxlink_host`) и подменяет консольный sink логов на stdout. На этом ПК ufw включён: для лога нужен входящий TCP 28771 из LAN; UDP-поиск консоли может не пройти (ufw, tun0), тогда указывать IP.

Снимки экрана (тот же день): при запуске через nxlink поток игры раз в `--mgrr_nxlink_shot_s` секунд (30 по умолчанию, 0 — выкл.) и по L+R+Minus снимает экран через caps:sc (`capsscCaptureJpegScreenShot`, весь экран с оверлеями) и пишет его в тот же stdout строками по 2 КБ base64: `@@MGRR_SHOT <имя> <часть> <частей> <crc32> <данные>` (первый вариант одной строкой на ~470 КБ на консоли дважды из десяти разорвался). `tools/nxlink_recv.py` раскладывает поток в `build/nxlink_logs/<время>/`: `log.txt` и `shot_NNN_{auto,button}.jpg`. Отдельный порт не нужен. Ctrl+C останавливает nxlink, приёмник дочитывает до конца.

## Логи SDK на английском (2026-10-21)

Сообщения рендерера deko3d, ввода-вывода, аудио и сохранений в rexglue-sdk-nx написаны по-испански. `tools/sdk_patches/0002-english-log-messages.patch` переводит 297 строк в 22 файлах: только строковые литералы в вызовах логов (REXLOG_*, REXKRNL_*, Registro, Rechazar, X360_INFORME...) и значения, которые туда подставляются. Проверено скриптом: код вне литералов байт в байт тот же, плейсхолдеры `{}`/`%` совпадают по числу и порядку. Не переведено (в логах MGR не встречалось): switch_perf (rex_perfil.log), apm, saltynx, sysclk, ошибки glsl/cache_dksh.

## T1 — запись кадров GPU на PC (2026-10-22)

Зачем: игра целиком в Ryujinx не идёт (SDK держит гостевую память на ловле ошибок доступа, а Ryujinx их программе не передаёт: в нём нет `svcReturnFromException`). Рендер отлаживаем повтором записанных кадров (PLAN.md, T1–T4).
- SDK `gpu_trace` (`sdk/include/rex/graphics/gpu_trace.h`, патч `tools/sdk_patches/0006-…`): `--gpu_trace_path=F --gpu_trace_start=N --gpu_trace_frames=K` пишет K кадров после swap N: регистры на начало, каждый пакет основного кольца, перед ним — прочитанная GPU память (`SharedMemory::RequestRanges`, `INDIRECT_BUFFER`, `IM_LOAD`), блоки с дедупликацией по XXH3.
- Игра пишет `F.shaders` (таблица копий микрокода: физ. адрес, размер, хеш контейнера, имя) через ~60 кадров после конца записи.
- Стартовый экран, swap 2400, 3 кадра: 48 пакетов кольца, 84 косвенных буфера, 1682 draw, 464 IM_LOAD, 1780 чтений; 69 МБ (почти всё — первый кадр: текстуры). Таблица шейдеров — 1241 копия. Разбор: `python3 -I tools/gputrace.py F`.
- Для Ryujinx в SDK ещё два патча, на консоли ничего не меняют: `0004` (нет argv — путь NRO по умолчанию от игры) и `0005` (нет хэндла процесса от загрузчика — получаем свой через IPC, как hbloader; до блокировки памяти, иначе дедлок в `threadCreate`).

## T2–T4 — декодер, плеер, автопрогон (2026-10-22)

- T2: разбор PM4 вынесен в `game/src/mgrr_pm4.{h,cpp}` (`mgrr::pm4::Decoder` + `Host`: прерывания, ожидание WAIT_REG_MEM, счётчик, рендерер, Swap). `mgrr_gpu.cpp` оставил себе MMIO, потоки, кольцо и отчёты. Поведение то же; на консоли не проверено.
- T3: режим `--mgrr_replay=F.gtr` того же NRO (`mgrr_replay.{h,cpp}`). `MgrrApp::OnFinalizePaths` возвращает nullopt, поэтому рантайм и гостевая память не создаются, а окно и презентер deko3d есть. Физическая память — свой буфер 512 МБ (SDK `0007`: `Memory::InitializePhysicalOnly`). Таблица шейдеров `F.shaders` → `AddCopy`. Запись играется `--mgrr_replay_passes` раз (по умолчанию 3: конвейеры компилируются в фоне). Выход `--mgrr_replay_out` (по умолчанию `F.out/`): `pass<P>_frame<N>.png` (PNG без сжатия, свой кодер) и `report.txt`.
- В записи не хватало вершин прямоугольника резолва: Xenia берёт его из ножниц и эту память не читает, а наш рендерер берёт из fetch-константы 0 → «empty copy rectangle (causa 6)», кадр не выводился. Добавлен хук `gpu_trace::Draw()` (патч 0006): в режиме копии записывает эти вершины.
- Ryujinx падал в `ConstantBufferUpdater.Update` («Destination is too short»): его кэш констант — 512 слов, а мы пушили до 4 КБ за раз. SDK `0008`: пуш не больше 2 КБ (на консоли то же самое, просто больше команд).
- Эталон: PC при записи сохраняет `F.frame<N>.ppm` — картинку презентера Xenia в конце каждого записанного кадра (`CaptureGuestOutput`). Плагин GPU грузится из `build/linux/librexgpu-xenos.so` — это копия, её обновляет только перелинковка `mgrr`; после правок SDK копировать вручную.
- T4: `tools/emu_test.sh F.gtr [проходы]` → `build/emu_runs/<время>/`: PNG, `report.txt`, `mgrr.log`, `compare_frame<N>.png`, `summary.txt`.
- Первый результат (стартовый экран, `title4_2400.gtr`): ~561 draw на кадр, 104 без шейдеров, отказы только 114/214 (`32_FLOAT`). Нарисованы логотип и фоновая панель; нет 3D-Райдена, дождя, «Нажмите старт» и копирайта. Разница: 29 из 255 в среднем, 31 % светлых пикселей эталона отсутствует.
- Известное: Ryujinx в конце падает на нулевом адресе при выходе приложения (результаты к этому моменту уже записаны).


## Стенд: от панели до полного стартового экрана; консоль рисует игру (2026-10-22)

Стартовый экран на стенде (`title6_2400.gtr`, кадр 2): средняя разница 29 → 2,6 из 255, пикселей с расхождением >32: 31 % → 1,4 %. По шагам:
- **Запись неполная.** Xenia читает из памяти только то, что загружает заново; текстуры, индексы и вершинные буферы, загруженные до записи, в неё не попадали (в повторе — нули). Теперь `gpu_trace` пишет каждую текстуру (`TextureCache::RequestTextures`), каждый вершинный буфер (до проверки «в синхроне», Vulkan `command_processor.cpp`) и индексы (`gpu_trace::Draw`) на каждом draw.
- **Очистки D3D.** D3D очищает цели рисованием прямоугольника своими встроенными шейдерами (4x MSAA-поверхность половинного pitch поверх той же EDRAM). Они не в библиотеке → не рисовались → у моделей не проходил тест трафарета/глубины, Райден исчезал. Перенесено из ds2-nx (`ds2_gpu.cpp`, GPL-3): `Decoder::EmulateClear` и `AliasScope` (MSAA → 1x). Буфер под прямоугольник даёт `Host::ClearRectangle` (в плеере — последняя страница физической памяти).
- **Встроенные VS из командного буфера** (IM_LOAD_IMMEDIATE): D3D переставляет в них vfetch и выкидывает экспорты, которые PS не читает. `Shaders::Identify` пропускает инструкции выборки целиком и берёт ближайший контейнер, если вне них отличается не больше 1/20 слов (волосы, глаза, z-map Райдена). Остальные ~100 draw/кадр без шейдеров — проверки видимости точками внутренними шейдерами D3D (запись цвета выключена).
- **Неявные текстуры.** `filtershadergathernoise` читает s1 (блум), которого нет в таблице констант; рендерер привязывает только объявленные. `ReadContainer` теперь добавляет все tfetch из микрокода (`AddFetchedSamplers`, разбор exec-инструкций).
- **Связка шейдеров по слоту, не по семантике.** Красное свечение и полоса через глаз — `falsevolumeparticle` (draw 410–457). VS `espshaderbase` пишет цвет в слот 1 как TEXCOORD1, PS читает слот 1 как TEXCOORD7; Xenos связывает по номеру слота (D3D экспорты VS здесь не трогал — проверено), SPIR-V из XenosRecomp — по имени, PS получал 0. В `tools/shaders/xenosrecomp/shader_recompiler.cpp` TEXCOORD и COLOR называются по номеру слота с обеих сторон; библиотека пересобрана (1482 из 1492, все в DKSH).
- Рендерер ds2-nx (`0002-deko-formats.patch`, та же база SDK f961303) влит трёхсторонне → `tools/sdk_patches/0009`: форматы 32_FLOAT и др., знаковые текстуры, копии в срезы 3D, частичные копии.

Инструменты:
- `--readback_resolve=full` + `gpu_trace`: эталон каждой копии в кадре → `F.res/` (+ `index.txt`); `tools/resolve_ref.py F КАДР OUT` → `ref<NNN>.png` и альфа. Нумерация шагов совпадает с плеером.
- Плеер: `mgrr_replay_steps=КАДР` (каждая копия → `step<NNN>.png`, каждый draw — строка в `report.txt`: шейдеры, цели, смешивание, глубина, текстуры с меткой ZERO, константы), `mgrr_replay_probes` (снимок цели после draw + позиции/потоки вершин/константы выборки), `mgrr_replay_plain` (draw без глубины/трафарета/смешивания), `mgrr_replay_plain_constant`, `mgrr_replay_plain_texture`, `mgrr_replay_dump` (дамп кадра рендерера; в Ryujinx валит сам эмулятор). В `emu_test.sh`: `PROBES=`, `PLAIN=`, `DUMP=`, `EXTRA=`.
- `--gpu_trace_skip=a-b,n` на PC — Xenia не исполняет эти draw: бисекция «кто рисует X» прямо в эталоне. `--gpu_trace_start=0` + F9 (`--mgrr_trace_key`) — запись по нажатию в `F_<N>.gtr`; таблица `.shaders` пишется по `gpu_trace_last`.
- Ловушки: убитые `pkill` прогоны PC оставляют `/dev/shm/xenia_memory_*` (по 4,8 ГБ), набралось 6 ГБ → следующий запуск падал с Bus error в `Memory::Initialize`. `pkill -f <шаблон>` убивает и свою оболочку (exit 144).

Консоль (Erista, `mgrr_clang.nro` 36f9ba32 / c1cb3553):
- Стартовый экран 30 кадров/с, 0 отказов; обучение ~60 кадров/с; миссия R-00 38–45 кадров/с при CPU 1785 / GPU 921 МГц, 800–1400 draw/кадр, GPU 12–15 мс/кадр.
- Чёрный экран при входе в миссию: `Thread creation failed: C0000017`. Каждый поток гостя получал стек хоста 16 МБ, Horizon выделяет его целиком. `--guest_thread_host_stack_kb` (Switch 4096) — миссия грузится; при отказе в лог пишутся память процесса, лимит потоков и куча (`tools/sdk_patches/0010`).
- Открыто: чёрное мерцание на лице Райдена (на PC нет), фризы, квадратные тени (на PC такие же — не наша ошибка).

## Мерцание лица, память, шейдеры наперёд (2026-10-22/23)

- **Чёрные точки на лице Райдена — NaN.** На Xenos (как в D3D9) 0 × что угодно = 0, даже ∞ и NaN; по IEEE 0 × ∞ = NaN, и пиксель чёрный. Xenia это правило эмулирует, XenosRecomp — нет. Теперь `Mul`, `Mad`, `Dp2Add/Dp3/Dp4`, `Dst` и скалярные `Mul` идут через `mulz`/`madz`/`dotz` (`tools/shaders/xenosrecomp/shader_common.h`). На стенде точки исчезли (стартовый экран 1,3 %), на консоли мерцания нет, кадров/с столько же. Цена: библиотека 15,9 → 19,7 МБ, uam на PC 16 → 44 с.
- **Зависание при выходе из миссии в меню** (чёрный экран): `CreateThread failed ... process memory 3185 of 3189 MB, heap 1620 MB`. `BaseHeap::Release/Decommit` не доходили до хоста, и на Switch каждая страница гостя, которой игра хоть раз коснулась, так и оставалась в куче. Теперь освобождаются куски, целиком лежащие в освобождённом диапазоне (`tools/sdk_patches/0011`, `--switch_release_guest_memory`). Каждые 10 с в логе строка `[mem]`: память процесса, куча, память гостя. На консоли ещё не проверено.
- **Чёрный экран на загрузке сцены.** Шейдеры, которых нет в кэше DKSH, компилировались на месте в потоке кольца, 10–200 мс каждый; после смены библиотеки за прогон таких набралось 259. Теперь компилируются наперёд в фоне: сначала те, что игра создаёт при загрузке сцены (`Shaders::TakeCreated`), потом вся библиотека, не больше двух в очереди. Компиляция, которую ждёт draw, идёт вне очереди (`tools/sdk_patches/0012`, `--deko_precompilar`). На консоли ещё не проверено.
- Игра целиком в Ryujinx по-прежнему не идёт (InvalidAccess на 4-й секунде, с освобождением памяти и без него) — проверка только на консоли.
- **Простой GPU 5–6 мс/кадр в миссии.** Рендер и презентер работают в одной очереди deko3d, и `Dispositivo::Adquirir` ставил в неё ожидание освобождения кадра swapchain (`dkQueueWaitFence`): до vblank очередь держала и следующие кадры игры. Теперь кадр ждёт поток UI на CPU (`dkFenceWait`), время ожидания пишется в отчёт («the UI thread for a swapchain image»). В миссии GPU был занят ~21 мс + 5–6 мс простоя = 36–40 кадров/с; ждём ~45. На консоли ещё не проверено.
- **Маска теней: фон чёрный вместо белого.** Самый тяжёлый draw кадра — `modelshadernewshadowreceiver` (полноэкранная маска, 2 мс, упирается в ALU). За пределами каскада теней он отбрасывает пиксели, и остаётся то, чем EDRAM очищена: D3D-очистка с MSAA (draw 58) пишет только глубину и трафарет по базе 0x530, а маска потом читает те же байты как цвет 8888. Глубину очистки мы угадывали («обратный Z → 0»), цвет поверх очищали в 0 → вне каскада всё затенено (вероятно, «тёмная зона» в миссии). Теперь `Decoder::ClearVertex` читает значения из вершин очистки (float3 позиция + float4 цвет; цвет — мини-выборка из того же буфера): z → D24S8/D24FS8, цвет поверх глубины = слово глубины и трафарета (`FFFFFFFF`). Маска на стенде совпала с эталоном (0,6 % пикселей >32 — края волос).
- Открыто: тёмная зона перед Райденом в миссии (похоже на маску теней: на стенде её фон чёрный, в эталоне белый — нужна запись кадра миссии на PC, клавиша Insert); GPU в миссии 18–25 мс/кадр, из них ~5 мс простоя между заданиями.

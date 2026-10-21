# MGR Revengeance (Nvidia Shield, v40) — структура

Это НЕ нативный Android-порт. Это x86 Windows-сборка PC-игры (MGRR.exe, PE32, VS2010, 23.12.2015),
запускаемая на ARM через ExaGear (x86→ARM) + TransGaming Cedega (Wine) + свой D3D9→GLES2 драйвер.

## Слои (сверху вниз)
1. MGRR.exe (x86, PlatinumGames engine; CRI Atom/Mana/CPK/ADX, Wwise (AK::), Havok, d3d9, XInput 1.3, DInput8, WS2_32)
2. Cedega/Wine (obb/patch: engine/bin/cedega, wineserver, engine/lib/*.so ~289 шт.; libd3dgl*.so = D3D9→GL, libwine_xaudio.so, libdsound.so)
3. ExaGear "UBT" (armeabi-v7a: libubt*.so) — динамический бинарный транслятор + эмуляция Linux-syscalls/VFS (lkv_*), vpaths
4. Android-обвязка: libubtlauncher.so (EGL/GLES2/OpenSL ES, TG3D драйвер ANDROIDCLIENT_TG3D*), libubtjni.so
5. Java: MainActivity, UE3JavaDownloader* (Play Expansion), NvidiaGPGS, InputSystem, MoviePlayer, DeviceInfo

## OBB
- main.27 (4.78 ГБ, FAT32 образ): windows/ (system32 dll'ки, Fonts), MGRR/ (MGRR.exe, libmmd.dll, svml_dispmd.dll, GameData/, movie_native/*.mp4)
- patch.40 (57 МБ, FAT16 образ): Ubuntu 12.04 i386 rootfs-срез (ld-linux.so.2, libc, libjpeg…), engine/ (cedega, wineserver, libs), transgaming/ (*.reg, config, nv_shaderstate.*), .exagear/vpaths-list
- Распаковка: 7z не открывает main (придирается к метке тома) → scratchpad/fatx.py (свой read-only FAT-экстрактор). Результат в obb_extracted/{main,patch}

## GameData
- data000..006, data104..109.cpk — CRI CPK
- sound/: stream*.cpk (в т.ч. DLC1-3, English(US)), Init*.bnk, Wwise*.wai, SeCollision/SeFootstep .bin, bgm/*.bnk
- movie/, movie_ui/ (.usm — CRI Sofdec2/Mana), movie_native/*.mp4 (H.264 катсцены, Android-специфично)
- shader.dat, shader2.dat, shadereff.dat; logo/{nvidia,transgaming}.wtb

# Xbox 360 версия (mvl-mgrr.iso, XGD3)
- Образ: XDVDFS со смещением 0x2080000 (XGD3). Экстрактор: scratchpad/xdvdfs.py. Распаковано в x360_extracted/ (default.xex, media/*; $SystemUpdate удалён)
- default.xex: XEX2 retail, AES+LZX, TitleID 4B4E080A, база 0x82000000, entry 0x82302C28, образ 27 525 120 Б
- Расшифрованный образ: x360_work/mgrr_image.bin (полный образ с базы 0x82000000)
- Секции: .rdata 0x82000400, .pdata 0x821F3A00 (57 025 функций), .text 0x82270000 (0xF8D75C ≈16,3 МБ ≈ 4,08 млн инстр.), .data 0x83200000, .idata 0x839B0000
- Middleware (те же сборки, что в PC): CRI Atom/ADX/ASR Sep 2012 (XBOX360), Wwise (AK::), Havok 3.1.0; 529 RTTI-типов
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
- Это тонкие функции: «сравнить с кэшем состояния → вызвать 1–2 функции D3D-ядра». У PC то же самое (обёртка → vtable IDirect3DDevice9), в PC-обёртках видны номера D3DRS_* — это готовый словарь семантики.
- Файлы: x360_work/WRAPPERS.md (словарь PC + гипотезы по 360), pc_wrappers.json, x360_wrapper_profiles.json, func_pairs.json (2 182 пары функций по общим строкам), matched.json.
- Инструменты: tools/pc_d3d_wrappers.py, pc_args.py, call_args.py, align_funcs.py, propagate.py, wrapper_match.py, sigmatch.py.
- Подтверждено (high): SetPixelShaderConstantF = 0x82796620 (ядро 0x826AC190); SetVertexShaderConstantF = 0x827966A0 (ядро 0x826AC0B8).
- Вероятно: Clear = 0x82791750; SetStencilWriteMask = 0x82790AA0; создание текстур/целей = 0x82792740, 0x82795548.
- Не получилось: сопоставить булевы/enum-состояния (cull, z, blend…) по числу вызовов или аргументам — у 360 набор обёрток другой, совпадений по статистике недостаточно. Совпадение функций по строкам попало почти целиком в код ресурсов, не рендера (рендер-функции без строк).
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
- 74 декларации в образе 360 (x360_vertex_decls.json) и 73 в PC (pc_vertex_decls.json, добыты из массивов D3DVERTEXELEMENT9 в PC-exe).
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

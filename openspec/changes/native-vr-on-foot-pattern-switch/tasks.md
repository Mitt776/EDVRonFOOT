# Tasks

## 1. Код и стенд

- [x] 1.1 В `edvr/src/d3d11/native_vr_probe_core.h` добавить чистый поиск байта кинотеатра: узор `FF [90-97] B0 00 00 00 [44|45] 38 [05|0D|15|1D|25|2D|35|3D] <disp32> 74 ?? 84 C0 75` по исполняемым разделам, RVA байта = конец `cmp` + `disp32`, результат «найден / не найден / найдено N / вне образа / раздел без записи» и текст строки результата; удалить `kCinemaSwitchRva`. `formatSwitch` получает найденный RVA, «not build 332841's 1» заменить на «not the game's 1». Проверка: стенд собирается
- [x] 1.2 В стенд `edvr/tools/native_vr_probe_test` добавить часть G на синтетических образах: одно совпадение (верный RVA), ноль, два, байт в разделе без записи, байт вне образа, другой регистр в `call` и `cmp`; строки результата и `formatSwitch` с найденным адресом. Обновить ожидания части F, если тексты строк изменились. Проверка: стенд проходит, число PASS больше 169
- [x] 1.3 В `edvr/src/d3d11/native_vr_probe.cpp` искать байт при первой установке (`tried`) в памяти загруженного образа под SEH, независимо от проверки сборки для зонда; передавать найденный адрес в `nativeVrCinemaSwitchStep`; одна строка результата с временем поиска; строку «stereo mode source is not hooked ... the game keeps its cinema» при найденном байте заменить на «the hold (build 332841 only) is not installed; the head camera switch works without it». Зонд и удержание по-прежнему только для 332841. Проверка: grep — `kCinemaSwitchRva` и «not build 332841's» в `edvr/src` и стендах не встречаются; стенд проходит
- [x] 1.4 Проверить узор на настоящем exe 334473 тем же правилом, что в коде (скрипт в скретчпаде, только чтение). Проверка: одно совпадение, байт `0x5E85B8A` в `.data` с правом записи, значение 1 — записано в исследование

## 2. Документы, сборка, установка

- [x] 2.1 README `edvr/README_EDVRONFOOT.md`, раздел `native_vr_on_foot`: байт ищется по коду игры, работает на 334473, какую строку искать в логе, что будет на сборке, где узор не найден; зонд и удержание — только 332841. Исследование: `## Status` — «.8, байт по узору». Проверка: в README нет «только сборка 332841» для `native_vr_on_foot`; `## Status` не длиннее 60 строк
- [x] 2.2 Полная сборка `cmd /c "D:\Project VSC\EDVRonFOOT\edvr\build_edvronfoot.bat"`, коммит, тег `v0.19.1-edvronfoot.8`, `build.bat --dll-only`. Проверка: «Native OpenXR build and all gates passed», единственный `KNOWN FAILURE tolerated: weapon_motion`, `config contract ok`
- [x] 2.4 (добавлено 2026-10-20, решение Димы) Установщик EDVR отказывает сборке 334473: он сверяет SHA-256 exe ради Oculus-маршрута. В `edvr/tools/install_edvr.py` добавить ключ `--allow-unqualified-game`, по умолчанию выключенный. С ним другая ревизия Odyssey принимается с предупреждением, а Horizons, не-Odyssey и нечитаемый exe отклоняются как раньше. Плюс самопроверка, README, повторная полная сборка, коммит, тег `.8` перенести на новый коммит (ещё не отправлен), `--dll-only`. Проверка: `install_edvr.py --self-test` ok; без ключа exe 334473 отклоняется, с ключом проходит с предупреждением; «all gates passed»
- [x] 2.3 Установка при закрытой игре: `py -3.12 edvr\tools\install_edvr.py --target "C:\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64" --all --tag edvronfoot --allow-unqualified-game --dry-run`, затем без `--dry-run`. Восстановить живой `edvr.ini` копией `%TEMP%\claude\edvr_ini_before_recon.ini` до первого запуска. Проверка: `edvronfoot_game.ps1 -Action Status` показывает `.8`; хэш `edvr.ini` в игре совпадает с копией; `Openvr\win64\openvr_api.dll` — от EDVR (версия, не игровой 311104 байт)

## 3. Проверка

- [ ] 3.1 Полёт Димы на `.8`: запуск → VR в корабле → на ноги (стерео, голова, глаза как на `.7`) → в корабль → выход. Проверка по логу (`edvr_log.py --expect-build HEAD`): версия `.8`, строка «cinema-on-foot switch found at EliteDangerous64.exe+0x5E85B8A», `head camera on: ... 1 -> 0`, на ногах `input 2720x2976`; отказы остальных частей EDVR из-за новой сборки выписаны в исследование отдельным списком

## Workflow follow-up

- Архивировать заявку после полёта (`/opsx:archive native-vr-on-foot-pattern-switch`).
- В заявке `native-vr-on-foot-ui-recon` задачи 1.1 (сборка) и 1.2 (строки переписи в `edvr.ini`) сделаны на `.7` до обновления, а обновление удалило `edvr.ini`. Их надо снять с отметки и повторить на `.8`. Полёт разведки можно провести сразу после 3.1.

# Tasks

## 1. Модуль `native_vr_on_foot` (переключатели) вместо `native_vr_probe`

- [x] 1.1 `edvr/src/d3d11/native_vr_on_foot_core.h` (design, решения 1, 3): из `native_vr_probe_core.h` перенести
  дословно часть переключателей (строки ~532–884: `onFootText`, `formatOnFootKey`, байт кинотеатра, `ImageSection`,
  `SwitchFind`, `PatternShape`, `findPatternTarget`, `findCinemaSwitch`, число HUD, `findHudSwitch`, `formatHudFind`,
  `formatFind`, `SwitchState`, `switchStep`, `cinemaSwitchStep`, `hudSwitchStep`, `switchAgainSaid`, `formatSwitch`,
  `formatHeadCameraJournal`, `formatHudSwitch`) и то, на что она опирается (`formatAddress`, если нужен).
  Пространство имён `nvf`. `formatOnFootKey` — текст без удержания (решение 3); `formatGameHudKey` не переносится.
  Проверка: заголовок компилируется в стенде 1.3; `Grep` по нему не находит `hold`, `ring`, `332841`, `game_hud`.
- [x] 1.2 `edvr/src/d3d11/native_vr_on_foot.{h,cpp}` (решения 1, 2): перенести дословно `nativeVrCinemaSwitchFind/Step`,
  `nativeVrHudSwitchFind/Step` с помощниками под SEH (`sehFindSwitch`, `sehReadByte`, `sehSwapByte`, `sehReadInt`,
  `sehSwapInt`) и `say`/`logSink`. Новая `nativeVrOnFootFrameBoundary`: только ключ `native_vr_on_foot` (нет — 1), поиск
  обоих переключателей при первом включении, шаг обоих каждый кадр с одним ключом. `device_hook.cpp`: include и тик
  `tkNativeVrOnFoot` ("native_vr_on_foot") вместо `tkNativeVrProbe`. Удалить `native_vr_probe.{h,cpp}`,
  `native_vr_probe_core.h`. Комментарии `debug_register_core.h` (кто пользуется слотами) — без `native_vr_probe`.
  Проверка: `Grep` по `edvr/src` и `edvr/tools` не находит `native_vr_probe`, `nativeVrProbe`, `native_vr_on_foot_game_hud`.
- [x] 1.3 Стенд `edvr/tools/native_vr_on_foot_test` (решение 5): части G и H старого стенда с их помощниками; части A–F
  не переносятся. `build.bat`: раздел `:rig_native_vr_on_foot_test` вместо `:rig_native_vr_probe_test`, линкует
  `native_vr_on_foot.cpp`; в списке исходников DLL `native_vr_on_foot.cpp` вместо `native_vr_probe.cpp`. Удалить
  `tools/native_vr_probe_test`. Проверка: стенд проходит; число вызовов `check` в частях G и H до и после переноса
  одно и то же (посчитать в файлах и записать в отчёт).
  Сделано: перенесена и часть F — это проверки переключателя кинотеатра (шаг, строки, клей), не зонда; без неё
  кинотеатр остался бы без половины проверок. Вызовов `check` до/после: F 30/30, G 36/36, H 38/35 (−3 — проверки
  текста `formatGameHudKey` удалённого ключа). Ожидаемый текст отказа в F: «the game keeps its cinema on foot» вместо
  «the hold works as before». Стенд: `PASS: native_vr_on_foot_test, 103 checks, 0 failed`.

## 2. Удаление зонда HUD

- [x] 2.1 Удалить `edvr/src/d3d11/on_foot_hud_probe.{h,cpp}`, `on_foot_hud_probe_core.h`, `on_foot_hud_layout.{h,cpp}`,
  `edvr/tools/on_foot_hud_probe_test` (решение 4). `vscreen.cpp`: три include, вызов в пути рисования
  (`if (onFootHudProbeWantsDraws()) …`), функция `onFootHudProbeNoteDraw`; из объявления-вперёд `pureDrawReissue`
  убрать слова про зонд, сама функция остаётся. `device_hook.cpp`: два include, тик `tkOnFootHudProbe`, сохранение
  кода шейдеров (`onFootHudProbeKeepsCode`). `build.bat`: два исходника из списка DLL, раздел
  `:rig_on_foot_hud_probe_test`. Комментарии `on_foot_markers*` и `on_foot_hud_core.h` с упоминанием зонда — по факту.
  Проверка: `Grep` по `edvr/src`, `edvr/tools`, `edvr/build.bat` не находит `on_foot_hud_probe`, `onFootHudProbe`,
  `on_foot_hud_layout`, `onFootHudLayout`, `ohp::`.

## 3. Ключи и документы

- [x] 3.1 Шаблон `edvr/edvr.ini` (решение 6): убрать `native_vr_probe`, `on_foot_hud_probe`, `native_vr_on_foot_game_hud`
  с их описаниями (секция `[experimental]`, около строк 1713–1761); заголовок блока «Native VR on foot probe» —
  по смыслу. Проверка: в выводе полной сборки `config contract ok: 129 keys`.
- [x] 3.2 `edvr/README_EDVRONFOOT.md`: убрать раздел «Зонд «нативный VR на ногах»»; в разделе «Нативный VR на ногах»
  убрать страховку-удержание и строки зонда, про HUD игры — что он включён всегда при `native_vr_on_foot`; что искать в
  логе — строки `native-vr-on-foot:` как в решении 3. `docs/research/native-vr-on-foot.md`, `## Status`: ключи — зонды и
  `native_vr_on_foot_game_hud` удалены (решение Димы 26.10), `native_vr_on_foot_hud_size` остаётся; следующий шаг —
  прогулка `.26`. Проверка: `py -3.12 edvr\tools\check_status_blocks.py` — все блоки не длиннее 60 строк.

## 4. Сборка и установка

- [ ] 4.1 Полная сборка. В PowerShell: PATH начинается с `C:\Users\user\AppData\Local\Programs\Python\Python312`,
  `TEMP`/`TMP` = `D:\Project VSC\EDVRonFOOT\edvr\build\tmp`, затем `cmd /c '"D:\Project VSC\EDVRonFOOT\edvr\build.bat"
  --jobs 8'`. Проверка: «all gates passed», квитанция `full-pass`, `config contract ok: 129 keys`; стенд
  `native_vr_on_foot_test` в выводе, старых двух нет. Коммит через `git commit -F`, тег `v0.19.1-edvronfoot.26`;
  `build.bat --dll-only` проходит. Нестабильные под нагрузкой (`flat_temporal_test`, `terrain_checkerboard_test` T5c):
  при сбое повторить сборку один раз и записать это в отчёт.
- [ ] 4.2 Установка при закрытой игре: `py -3.12 edvr\tools\install_edvr.py --target "C:\Steam\steamapps\common\Elite
  Dangerous\Products\elite-dangerous-odyssey-64" --all --tag edvronfoot --keep-backups 1000 --dry-run`, затем без
  `--dry-run`. Проверка: `--verify-only` ok; хеш живого `edvr.ini` до и после установки один.
- [ ] 4.3 Живой `edvr.ini` в папке игры: копия в scratchpad, затем Edit убирает три строки `native_vr_probe = …`,
  `on_foot_hud_probe = …`, `native_vr_on_foot_game_hud = …` (с их комментариями, если они там есть). Проверка: дифф
  копии и файла — только эти строки.

## 5. Прогулка и итог

- [ ] 5.1 Прогулка Димы на `.26` (короткая):
  1. Запуск из Steam. В корабле — как обычно.
  2. Выйти на ноги (планета или станция): HUD на листе, прицел и подсказки игры над предметами видны.
  3. Подойти к лифту или двери корабля: меню на месте.
  4. Вернуться в корабль, выйти из игры через меню.

  Проверка: Дима рассказал, всё ли как на `.25` (HUD, прицел, подсказки, меню, стерео без кинотеатра).
- [ ] 5.2 Разбор: сначала `edvr_log.py --expect-build v0.19.1-edvronfoot.26` (не 2). По логу:
  - `native-vr-on-foot: cinema-on-foot switch found at EliteDangerous64.exe+0x5E85B8A` и `game HUD switch found at
    …+0x5E85B94`, затем `head camera on: … 1 -> 0` и `game HUD on: … -1 (auto) -> 1 (on)` — как на `.25`;
  - нет строк `native-vr-probe:`, `on-foot hud probe:`, `the hold`;
  - сводки листа HUD и меток как на `.25` (`game-composites=0`, `copy-fail=0`, метки «corrected» = шагам).

  Итог — в исследование разделом «Прогулка `.26`», в `## Status` — следующий шаг. Проверка: `## Status` не длиннее
  60 строк, итог объяснён Диме в чате.

## Workflow follow-up

- Архивировать (`/opsx:archive native-vr-on-foot-keys-cleanup`): `native-vr-probe` и `on-foot-hud-probe` выводятся из
  спек, дельты сливаются в `native-vr-on-foot` и `on-foot-hud`.
- Осталась тема просадок FPS на станции (кадр игры) — отдельная заявка, если Дима захочет.

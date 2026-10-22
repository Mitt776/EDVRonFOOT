# Tasks

## 1. Один режим: stereo без ключа

- [x] 1.1 В `edvr/src/d3d11/on_foot_markers_core.h` убрать ключ и `flat` (design D1).
  - Уходят: `Mode`, `Key`, `parseKey`, `formatKey`, `kWhyKey`.
  - `decide(native, onFoot)`, `correct(in, out)` всегда `stereo`.
  - `formatToggle` и `formatSummary` без слова режима.
  - Шапка файла без ключа.
  - Проверка: `grep -n "flat\|parseKey\|native_vr_on_foot_markers" edvr/src/d3d11/on_foot_markers*` пуст.
- [x] 1.2 В `edvr/src/d3d11/on_foot_markers.cpp` и `.h`:
  - `g_mode` → `g_on`, `onFootMarkersSetMode` → `onFootMarkersSetOn`;
  - `onFootMarkersFrameStep` без `keyText`;
  - `onFootMarkersFrameBoundary` не читает ключ;
  - шапка `.h` без ключа.

  Комментарий у вызова в `edvr/src/d3d11/device_hook.cpp` (около `tkOnFootMarkers`) — без «its keys».
  Проверка: тот же `grep` по `on_foot_markers*` и `device_hook.cpp` пуст.
- [x] 1.3 `SampleBox::reset()` (`Ready` → `Idle`). Шаг кадра зовёт `reset()` перед `want()` при каждом переходе
  в «вкл» (design D3). Проверка — тесты 3.1 и 3.2.
- [x] 1.4 Удалить блок `native_vr_on_foot_markers` (комментарий, `# ui:`, строку) из `edvr/edvr.ini` с Edit, затем
  сравнить до и после. Проверка: `grep -n native_vr_on_foot_markers edvr/edvr.ini` пуст;
  `py -3.12 tools\check_config_contract.py --self-test` из `edvr` проходит.

## 2. Поиск трёх точек по узору

- [x] 2.1 В `on_foot_markers_core.h` (design D2):
  - маскированные узоры четырёх точек из таблицы design;
  - `ofm::Section`, `findPoints(image, sections, count)` — один проход, ровно по одному совпадению на вход, три сверки
    через писателя позы с окнами `0x100` / `0x400`;
  - строки `formatFound` (три адреса, мс) и `formatNotFound` (точка или сверка, число совпадений).

  Уходят `kPointRva`, `kPointPrologue`, `kPrologueBytes`, `kExpectedTimestamp`, `kExpectedImageSize`, `formatWrongBuild`,
  `formatWrongBytes`. Проверка — тесты 3.1.
- [x] 2.2 В `on_foot_markers.cpp`:
  - обход заголовков PE и секций под SEH (по образцу `sehFindSwitch` в `native_vr_probe.cpp`);
  - один поиск на первом кадре с `native_vr_on_foot` = вкл, со строкой поиска и мс;
  - `onFootMarkersInstall` берёт адреса из результата поиска, проверки метки PE и 16 байт удалены;
  - отказ поиска ⇒ `off (not installed, the line above says why)`;
  - для стенда вход установки принимает базу и секции синтетического образа вместо `targets`.

  Проверка — тесты 3.2.
- [x] 2.3 Ключ стенда `--exe <путь>`: прочитать файл exe, разложить секции, напечатать `findPoints`.
  Проверка: на живом exe печатает `0x287D660`, `0x28457F0`, `0x2874820` и «found».

## 3. Стенд и сборка

- [x] 3.1 Часть A стенда `edvr/tools/on_foot_markers_test/on_foot_markers_test.cpp`:
  - убрать проверки ключа и `flat`;
  - `decide` по двум входам;
  - `findPoints` — все случаи из design D4;
  - `SampleBox` `Ready` → `reset` → новый образец.

  Порча одного ожидания даёт выход 1 (проверить один раз и вернуть).
- [x] 3.2 Часть B стенда:
  - синтетический образ с исполняемой секцией, где у писателей и фиксации байты входа игры, а у писателя позы есть
    цепочка и оба вызова;
  - установка через поиск;
  - отказы «нет узора» и «нет вызова» — ничего не пропатчено, одна строка;
  - шаг кадра без ключа;
  - корабль → ноги → корабль → ноги: первая сводка второго выхода несёт свежий образец;
  - три сбоя фиксации выключают правку.

  Проверка: `on_foot_markers_test.exe --self-test` — exit 0.
- [x] 3.3 Обновить `REM` гейта `:rig_on_foot_markers_test` в `edvr/build.bat` (без ключа и «другой сборки», с поиском).
  Запустить полную сборку `edvr\build.bat` по абсолютному пути и прочитать хвост. Проверка: all gates passed,
  `full_build_receipt.json` записан.

## 4. Документы, установка, проверка в игре

- [x] 4.1 В `docs/research/native-vr-on-foot.md`:
  - `## Status` — ключ меток убран, `stereo` — поведение, точки ищутся по узору; не длиннее 60 строк;
  - датированный раздел «Stereo как поведение `.18`» с таблицей узоров.

  Проверка: Status ≤ 60 строк.
- [x] 4.2 Коммит кода (сообщение через файл, `git commit -F`). Затем `build.bat --dll-only` по абсолютному пути,
  установка `py -3.12 tools\install_edvr.py --target "C:\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64" --all --tag edvronfoot`,
  `--verify-only`. Проверка: `vr payload and profile verified`, `edvronfoot_game.ps1 -Action Status` показывает
  новую версию.
- [ ] 4.3 Прогулка Димы по «Как проверить руками» из proposal. Разбор лога через `py -3.12 tools\edvr_log.py --target steam --expect-build HEAD`.
  Проверка:
  - строка `on-foot markers: found` с тремя адресами и мс;
  - строки ключа нет;
  - на ногах сводка `corrected` = `steps`, пропусков 0;
  - `frame check` — почти всё «ours»;
  - в F8 нет «On-foot markers»;
  - со слов Димы подсказки стоят на предметах.

  Итог — в раздел 4.1.

## Workflow follow-up

- `/opsx:archive` после прогулки: синхронизация дельты в `openspec/specs/native-vr-on-foot/spec.md`,
  `openspec validate native-vr-on-foot --type spec --strict`.

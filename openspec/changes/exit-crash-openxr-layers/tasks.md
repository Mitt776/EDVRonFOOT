# Tasks

## 1. Снимок «до» (без сборки)

- [x] 1.1 Записать исходную статистику, чтобы потом сравнить: число дампов в
  `%LOCALAPPDATA%\Temp\Frontier Developments\EliteDangerous\CrashDumps` (на 05.11 — 114, ~4 ГБ), число записей
  `UNHANDLED` в `edvr_breadcrumbs.txt` игры (65), время последней из них. Скрипт разбора дампа (`dmp.py`, `many.py`,
  `table.py` из scratchpad сессии 05.11) перенести в `edvr/tools/exit_crash_dump.py`, с `--self-test` по правилам
  `AGENTS.md`. Он печатает адрес падения, его младшие 16 бит, `rax − rcx`, соседей в карте модулей и цепочку стека.
  Проверка: `py -3.12 edvr\tools\exit_crash_dump.py --self-test` проходит. На свежем дампе печатает `…F780`,
  `rax-rcx=0x168` и цепочку `d3d11.dll+0x2EC9C4 → gameoverlayrenderer64.dll+0x71D05`.

## 2. Ядро и стенд

- [x] 2.1 Чистая половина `edvr/src/openxr/api_layer_pin_core.h` (design, решения 4–5): решение «модуль — слой», если
  у него есть экспорт `xrNegotiateLoaderApiLayerInterface`; сборка строки
  `openxr_layers,found=N,pinned=M,list=имя:pinned|имя:error=K` (только имя файла, без пути; `found=0` без списка).
  Проверка: компилируется в стенде 2.3.
- [x] 2.2 Windows-половина `edvr/src/openxr/api_layer_pin.h`: обход модулей `K32EnumProcessModules`, `GetProcAddress` на
  экспорт, закрепление `GetModuleHandleExW(FROM_ADDRESS | PIN)` по адресу экспорта, код ошибки при отказе, возврат
  списка для строки. Комментарий «зачем» — по образцу `edvr/src/common/module_pin.h`, со ссылкой на это исследование.
  Проверка: стенд 2.3.
- [x] 2.3 Стенд `edvr/tools/openxr_layer_pin_test` (design, решение 6): `layer_fixture.dll` с экспортом,
  `plain_fixture.dll` без него. После загрузки найден ровно слой. После закрепления и двух `FreeLibrary` слой в процессе,
  простая DLL выгружена. Строки для 0, 1 и 2 слоёв и для ошибки. Гейт `:rig_openxr_layer_pin_test` в `build.bat` рядом
  с `:rig_journal_unload_test`; правку `build.bat` сохранить с CRLF. Проверка: `PASS: openxr_layer_pin_test, N checks`.
  Сборка падает, если испортить одно ожидание (проверить один раз и вернуть).

## 3. Подключение

- [ ] 3.1 `edvr/src/openxr/native_runtime_host.h`, `open()`: сразу после удачного `api.createInstance` найти и закрепить
  слои, записать строку `openxr_layers,…` через `nativeTracePrintf`. Неудача не меняет исход `open()`. Порядок закрытия
  (`host_instance_destroy`, `host_loader_free`) не трогать. Перечитать по `AGENTS.md`. Проверка: полная сборка 4.2,
  все гейты OpenXR (`openxr_*_test`) проходят.

## 4. Документы, сборка, установка

- [x] 4.1 Документы:
  - `edvr/docs/shutdown-freelibrary-race-2026-09-21.md`: в `## Status` найденная причина (OpenXR Toolkit, Detours на
    `RSSetViewports`, `…F780` = RVA `0x8F780`, `rax − rcx = 0x168`, 31 мс после `host_loader_free`); в `## Ruled out` —
    «выгрузка системного `d3d11.dll`» (`pinned=1` во всех логах, а падение было);
  - `docs/research/native-vr-on-foot.md`: строку про сбой при выходе в разделе гипотез — на ссылку и итог;
  - ROADMAP, «Одна из последних заявок — падение при выходе»: статус;
  - `edvr/README_EDVRONFOOT.md`: короткий абзац «Падение при выходе и OpenXR Toolkit» и что искать в OpenXR-логе
    (`openxr_layers`).

  Проверка: `py -3.12 edvr\tools\check_status_blocks.py` проходит.
- [ ] 4.2 Полная сборка по `edvr/AGENTS.md`: `build.bat` по абсолютному пути, `--jobs 8`. Проверка: «all gates passed»,
  `config contract ok`, число ключей то же, что на `.51`. Дерево чистое, коммит через `git commit -F`, тег
  `v0.19.1-edvronfoot.52`; `build.bat --dll-only` проходит.
- [ ] 4.3 Установка при закрытой игре: `python tools\install_edvr.py --target steam --keep-backups 1000`, сначала
  `--dry-run`. Живой `edvr.ini` не трогать. Проверка: `--verify-only` ok, хеш `edvr.ini` до и после один и тот же.

## 5. Полёт и итог

- [ ] 5.1 Полёт Димы на `.52` (запуск из Steam как обычно; OpenXR Toolkit не трогать, оверлей Steam не выключать):
  1. Запустить игру, дождаться картинки в шлеме, 1–2 минуты полетать или постоять.
  2. Выйти из игры через меню («Выход» → «Выйти из игры»).
  3. Ещё раз: запуск → минута в игре (можно выйти на ноги) → выход через меню.
  4. Если не лень — третий раз.
  5. Сказать, как закрывалась игра: сразу, с задержкой, с окном ошибки.

  Проверка: Дима рассказал, как прошли выходы.
- [ ] 5.2 Разбор: сначала `edvr_log.py --target steam --expect-build v0.19.1-edvronfoot.52` (и `--tag openxr`). По логу:
  - в OpenXR-логе каждой сессии строка `openxr_layers,found=1,pinned=1,list=XR_APILAYER_MBUCCHIA_toolkit.dll:pinned`;
  - закрытие `shutdown_stage … ok=1` до `host_gate_finish`, `module_shutdown_return,exception=0`;
  - в `edvr_breadcrumbs.txt` после времени полёта нет новых `UNHANDLED`; в `CrashDumps` нет новых папок
    (сравнить с 1.1).

  Если падение есть — разобрать новый дамп `exit_crash_dump.py`, итог записать в исследование, решать с Димой.
  Итог — в `edvr/docs/shutdown-freelibrary-race-2026-09-21.md` разделом «Полёт `.52`» и в ROADMAP. Проверка:
  `check_status_blocks.py` проходит, итог объяснён Диме в чате.
- [ ] 5.3 Уборка дампов (решение Димы 05.11, только после удачного 5.2): показать Диме список папок `CrashDumps` с датами
  и общий размер. Оставить 3 самых свежих дампа падения (до правки). Остальные удалить только после явного «да» Димы
  в чате. Проверка: размер папки до и после назван в чате, 3 дампа на месте.

## Workflow follow-up

- После 5.3 — `/opsx:archive`: спека `game-exit` переносится в `openspec/specs/`.

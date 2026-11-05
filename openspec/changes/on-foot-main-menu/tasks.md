# Tasks

## 1. Журнал: признак «главное меню»

- [x] 1.1 `edvr/src/d3d11/journal_watch.cpp/.h` (design, решения 2–3): в `scanRange` тем же проходом искать кусок
  `"MusicTrack":"MainMenu"` и ставить `s.pub.mainMenu = true`; `LoadGame` и `Shutdown` снимают его. `Published`,
  атомик в `g_v`, `publish`, читатель `journalMainMenu()` (false у плоского профиля и при неактивном журнале).
  Комментарий «names only» в `onEvent` дополнить: одно значение, `MainMenu`, хранится флагом. Проверка: стенд 1.2.
- [x] 1.2 `tools/gate_test` (рядом с проверками `Disembark`/`Shutdown`, около строки 1260): `Music` с `MainMenu`
  ставит признак, `Music` с `Exploration` — нет, `LoadGame` снимает, `MainMenu` снова ставит, `Shutdown` снимает;
  `MainMenu` и `LoadGame` в одном дописанном куске — итог по последнему; кусок, разрезанный между двумя дописываниями,
  находится. Проверка: `gate_test` проходит; один раз испортить ожидание — падает, вернуть.

## 2. Модуль: переключатели в меню

- [x] 2.1 `edvr/src/d3d11/native_vr_on_foot_core.h` (design, решение 4): чистая половина — шаг признака меню
  (переход вход/выход при включённом ключе) и текст двух строк: `native-vr-on-foot: main menu (journal Music:
  MainMenu): both switches back to the game until the next LoadGame` и `native-vr-on-foot: LoadGame (journal): both
  switches ours again`. В `formatSwitch` / `formatHudSwitch` признак «меню»: на `kSwitchRestored` и `kSwitchSet`
  без строки, остальные виды как были. Проверка: стенд 2.3.
- [x] 2.2 `edvr/src/d3d11/native_vr_on_foot.cpp`: в `nativeVrOnFootFrameBoundary()` `held = onFootKey &&
  !journalMainMenu()`, шагам — `held` и признак меню; строка на переход. В стенде — заглушка `journalMainMenu()`
  рядом с `journalOnFoot()`. Перечитать по `AGENTS.md`. Проверка: стенд 2.3.
- [x] 2.3 `tools/native_vr_on_foot_test`: на байте и int стенда — с ключом и меню оба значения игры (1 и −1), после
  выхода из меню снова наши (0 и 1); без ключа меню ничего не пишет; строки меню и загрузки ровно по одной, строк
  «off …»/«on …» в этих переходах нет; значение, которое не наше, в меню не трогается (строка `kSwitchLeft` есть).
  Обновить `REM` гейта `:rig_native_vr_on_foot_test` в `build.bat` (с CRLF). Проверка: `PASS`; один раз испортить
  ожидание — падает, вернуть.

## 3. Документы, сборка, установка

- [x] 3.1 Документы: `docs/research/native-vr-on-foot.md` — раздел «Меню после ног (05.11)» с разбором (полёт
  `.52`, F8 → меню появилось, `Music: MainMenu`, `Status.json` поздно) и строка в `## Status`; ROADMAP — пункт про
  невидимое меню; `edvr/README_EDVRONFOOT.md` в разделе «Нативный VR на ногах» — абзац про главное меню и две строки
  лога. Проверка: `python tools\check_status_blocks.py` проходит, длина `Status` исследования не больше, чем до правки.
- [ ] 3.2 Полная сборка по `edvr/AGENTS.md`: `build.bat` по абсолютному пути, `--jobs 8`. Проверка: «all gates
  passed», `config contract ok: 131 keys` (как на `.52`). Коммит через `git commit -F`, тег `v0.19.1-edvronfoot.53`,
  `build.bat --dll-only` проходит.
- [ ] 3.3 Установка при закрытой игре: `python tools\install_edvr.py --target steam --keep-backups 1000`, сначала
  `--dry-run`. Живой `edvr.ini` не трогать. Проверка: `--verify-only` ok, хеш `edvr.ini` до и после один и тот же.

## 4. Полёт и итог

- [ ] 4.1 Полёт Димы на `.53` (F8 не трогать; PAUSE — метка в каждом пункте, когда видно результат):
  1. Загрузиться, выйти на ноги, выйти в главное меню («Выход» → «В главное меню»). Видно меню, стоит ли на месте
     при повороте головы.
  2. Из меню загрузиться снова (появишься на ногах). Стерео на ногах, HUD игры, не кинотеатр?
  3. Сесть в корабль, выйти в главное меню из корабля. Меню видно?
  4. Загрузиться в корабль, выйти на ноги. Стерео на ногах?
  5. Выйти из игры. Рассказать, что видел в каждом пункте.

  Проверка: Дима рассказал по каждому пункту.
- [ ] 4.2 Разбор: `edvr_log.py --target steam --expect-build v0.19.1-edvronfoot.53`. По логу: на каждый выход в меню
  строка `main menu (journal Music: MainMenu)`, на каждую загрузку после меню — `LoadGame (journal): both switches
  ours again`; в меню строк `head camera off` / `game HUD off` нет; на ногах после загрузки `head camera: on foot
  (journal) with the cinema-on-foot switch 0`. Итог — в исследование разделом «Полёт `.53`», в ROADMAP. Если в
  пункте 2 кинотеатр — design, риск 1: записать и решать с Димой. Проверка: итог объяснён Диме в чате.

## Workflow follow-up

- После 4.2 — `/opsx:archive`: требования переносятся в `openspec/specs/native-vr-on-foot/spec.md`.

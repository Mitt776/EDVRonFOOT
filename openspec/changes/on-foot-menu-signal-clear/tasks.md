# Tasks

## 1. Ветка и PR

- [x] 1.1 Ветка `change/on-foot-menu-signal-clear` от `main` (18.11, после слияния #10 и #11).
- [x] 1.2 Коммит заявки `git commit -F`, почта noreply; push; PR в `main` по шаблону: ссылка на заявку, план полёта.

## 2. Сборка

- [x] 2.1 `journal_watch.cpp`, `onEvent`: `Music` → `s.pub.mainMenu = false` (кусок `MainMenu` дальше в строке ставит
  снова); `Resurrect` → `false`. Комментарии в `Published::mainMenu`, `onEvent`, `scanRange`, строке `journal:` при старте,
  `journal_watch.h` (`journalMainMenu`).
- [x] 2.2 `native_vr_on_foot_core.h`, `formatMainMenu`: новые тексты (design, решение 4); комментарии «until the next
  LoadGame» в `native_vr_on_foot.cpp`, `on_foot_hud.cpp`, `on_foot_hud_core.h`.
- [x] 2.3 Стенд `gate_test`: строки журнала 18.11 (`MainMenu`, затем `Supercruise`) — признак снят; смерть (`MainMenu`,
  `Resurrect`) — снят; `Music: MainMenu` после другой музыки — снова стоит; `MainMenu` и `Friends` — стоит (меню не
  снимается чужим событием); прежние проверки (разрез куска, `LoadGame` в одном чтении) — как были.
- [x] 2.4 Стенд `native_vr_on_foot_test`: новые тексты строк.
- [x] 2.5 README (`edvr/README_EDVRONFOOT.md`, «Главное меню» и строки лога); `docs/research/native-vr-on-foot.md` —
  раздел «Ложная музыка меню» и строка в `## Status`; ROADMAP. `check_status_blocks.py` проходит.
- [ ] 2.6 Полная сборка `--jobs 8` → «all gates passed»; коммит; `--dll-only`; установка (`--dry-run`, затем установка,
  `--verify-only`, хеш `edvr.ini` тот же).

## 3. Полёт и разбор

- [ ] 3.1 Полёт Димы:
  1. Такси до станции (в суперкруизе один раз Esc на 10–20 с и закрыть). Выйти на ноги — стерео шлема, HUD игры, не
     кинотеатр.
  2. С ног выйти в главное меню — меню видно и стоит на месте. Загрузиться — на ногах стерео.
- [ ] 3.2 Разбор: `edvr_log.py --expect-build`; если в журнале была ложная `Music: MainMenu` — в логе пара строк `main menu
  …` и `the game goes on …` через секунды, выход на ноги в стерео (`head camera: on foot (journal) with the
  cinema-on-foot switch 0`). Настоящее меню: строка `main menu …`, после загрузки `the game goes on …`, лист HUD и метки
  с причиной `main menu`.

## 4. Закрытие

- [ ] 4.1 После «да» Димы: архив (спека `native-vr-on-foot` синхронизирована), слияние локально по `edvr/AGENTS.md`,
  push, проверка `git log origin/main -1`. Выпуск — по отдельному «да».

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
- [x] 2.6 Полная сборка `--jobs 8` → «all gates passed»; коммит; `--dll-only`; установка (`--dry-run`, затем установка,
  `--verify-only`, хеш `edvr.ini` тот же). (18.11: `2992017`, gate_test и native_vr_on_foot_test в полной сборке, verify`n  ok, хеш ini тот же.)

## 3. Полёт и разбор

- [x] 3.1 Полёт Димы:
  1. Такси до станции (в суперкруизе один раз Esc на 10–20 с и закрыть). Выйти на ноги — стерео шлема, HUD игры, не
     кинотеатр.
  2. С ног выйти в главное меню — меню видно и стоит на месте. Загрузиться — на ногах стерео.
- [x] 3.2 Разбор: `edvr_log.py --expect-build`; если в журнале была ложная `Music: MainMenu` — в логе пара строк `main menu
  …` и `the game goes on …` через секунды, выход на ноги в стерео (`head camera: on foot (journal) with the
  cinema-on-foot switch 0`). Настоящее меню: строка `main menu …`, после загрузки `the game goes on …`, лист HUD и метки
  с причиной `main menu`.

Итог 3.1/3.2 (18.11, лог `edvr_gfx_20261118_225416.log`, сборка `2992017`): ложная «музыка меню» повторилась —
журнал 16:19:26Z `Music: MainMenu` в суперкруизе такси, 16:19:36 `Music: Supercruise`. Лог: 23:19:27 `main menu …`,
23:19:36 `the game goes on …: both switches ours again` (через 9 с), 23:21:27 выход на станции — `head camera: on foot
(journal) with the cinema-on-foot switch 0`, стерео. Настоящее меню из корабля: 23:24:48 `main menu …`, 23:25:02
`Shutdown` (строка `the game goes on` пишется и на выходе из игры — безвредно). Дима: «ничего не сломалось».

## 4. Закрытие

- [x] 4.1 После «да» Димы: архив (спека `native-vr-on-foot` синхронизирована), слияние локально по `edvr/AGENTS.md`,
  push, проверка `git log origin/main -1`. Выпуск — по отдельному «да».

# Tasks

## 1. Ветка и PR

- [x] 1.1 Ветка `change/on-foot-side-choice-panel` от `change/on-foot-aim-dot-victory` (18.11). Коммит `git commit -F`,
  почта noreply. Push, PR в `main` по шаблону: ссылка на заявку, план полёта, пометка «после #10».

## 2. Сборка

- [x] 2.1 `on_foot_hud_core.h`: `Layer::Side = 10`, `kLayers = 11`; спека слоя `{1274/1201, 0.2018..0.7982, 0.0003..0.9997,
  kMenuCurve, "the side choice", "side", 5, 1274}`; `keepsPicture` с `Side`; `menus[560]`. `on_foot_hud.h`
  `kOnFootHudSheets = 11`, `menu_panel.h` `kMenuSheetLayers = 11`.
- [x] 2.2 Стенд `on_foot_hud_test`: `partSide` (форма, доля HUD, 4 рисования нет, без HUD нет, другое качество нет,
  качество 0.50..1.00, место, строки, сводка), размеры в таблице качеств, `partStats` (слой не последний, удержание у
  четырёх, сводка из десяти меню). PASS 800.
- [x] 2.3 Документы: `docs/research/native-vr-on-foot.md` (запись), ROADMAP. `check_status_blocks.py` проходит.
- [x] 2.4 Полная сборка `--jobs 8` → «all gates passed»; коммит; `--dll-only`; установка (`--dry-run`, `--verify-only`,
  хеш `edvr.ini` тот же). (18.11: `55cd031`, PR #11, verify ok, хеш ini тот же.)

## 3. Полёт и разбор

- [ ] 3.1 Полёт Димы (вместе с PR #10): нейтральным в наземной зоне конфликта `I` — панель выбора стороны видна по
  центру, сторону выбрать. Затем бой, победа, убрать оружие, `I`, закрыть статистику — точки нет до шаттла.
- [ ] 3.2 Разбор: `showing the side choice (1147x1081` (или другого размера при другом качестве), `kept` или `stopped`;
  сводка `side`. Для PR #10 — `far search: the plain dot at …`, `far_found` > 0.

## 4. Закрытие

- [ ] 4.1 После «да» Димы и слияния #10: архив, слияние локально по `edvr/AGENTS.md`, push. Выпуск — по отдельному «да».

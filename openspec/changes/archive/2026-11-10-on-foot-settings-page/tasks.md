# Tasks

## 1. Генератор и пометки

- [x] 1.1 `edvr/tools/gen_settings_schema.py`: добавить `on_foot` в `MENU_PAGES`. Разрешить токен `menu on_foot` у
  `[experimental]`-ключа: строка tier Fix на странице `on_foot`, не в перечне Experimental, попадает в `kSettings`.
  Любой другой `menu` у `[experimental]` — ошибка, как сейчас. Проверка: `python tools\gen_settings_schema.py
  --self-test` проходит.
- [x] 1.2 Там же три новых случая самопроверки (design, решение 4). Проверка: каждый случай, если его сломать
  нарочно, валит `--self-test`; после возврата всё проходит.
- [x] 1.3 `edvr/edvr.ini` (шаблон): `| menu on_foot` в строках `# ui:` пяти ключей `native_vr_on_foot*` (правка
  инструментом Edit, без regex). Проверка: в сгенерированном `build\gen\` пять строк `"on_foot"` и пять записей
  `experimental` в `kSettings`.

## 2. Меню F8

- [x] 2.1 `edvr/src/d3d11/menu.cpp`, `buildPages()`: страница On foot после Explorer Cam. Записать в
  `edvr/docs/settings-menu.md`, что такая страница есть. Проверка: полный `build.bat --jobs 8` по абсолютному пути
  зелёный, есть `build\full_build_receipt.json`.

## 3. Документы

- [x] 3.1 `GUIDE.md` и `GUIDE.ru.md`: вместо раздела про Developer mode — «F8 → страница On foot» и раздел
  «Native VR on foot (EDVRonFOOT)» в установщике; ссылки «On the Experimental page» поправить. Проверка: grep
  `Experimental` в обоих файлах больше не ведёт игрока к настройкам на ногах.
- [x] 3.2 `edvr/README_EDVRONFOOT.md` — то же, если там упомянута страница Experimental. `ROADMAP.md`: хвост
  «Настройки на ногах спрятаны» — ГОТОВО со ссылкой на заявку. Проверка: grep.

## 4. Сборка, установка, полёт

- [x] 4.1 Коммит в ветке `change/on-foot-settings-page`, PR в `main`. Затем `build.bat --dll-only`,
  `python tools\install_edvr.py --target steam --dry-run`, потом без `--dry-run`, потом `--verify-only`.
  Проверка: verify проходит, версия в логе совпадает с коммитом (`edvr_log.py --expect-build HEAD`).
- [x] 4.2 Полёт Димы (как проверить руками):
  1. **До установки** в установщике или в `edvr.ini` поставить On-foot aiming eye = `left`. После установки с «Keep
     the settings I have changed» в установщике должен остаться `left`, а раздел «Native VR on foot (EDVRonFOOT)» —
     показывать все пять настроек.
  2. В игре, Developer mode **выключен**: F8, листать Tab. Есть страница **On foot** с пятью строками, полоска
     страниц влезает по ширине.
  3. На ногах на странице On foot: On-foot aim dot → `show`, точка появилась; обратно `hide`. On-foot interface →
     `visor`, лист у глаз; обратно `body`. Aiming eye вернуть на `right`.
  4. Developer mode **включить**: на странице Experimental этих пяти строк нет, на On foot есть.

  Проверка: Дима подтверждает все четыре пункта. По логу: `failures=0`, нет ошибок меню.

## Workflow follow-up

- После полёта и «да» Димы: архив заявки в той же ветке, синхронизация спек (`on-foot-settings` новая, `on-foot-hud`
  изменена), слияние PR локально (`git merge --no-ff`), проверка почты коммитов, push.
- Выпуск 0.1.2 — только по слову Димы.

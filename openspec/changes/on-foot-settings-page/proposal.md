# Proposal

## Why

Пять настроек на ногах — главное, что даёт EDVRonFOOT, — игрок сейчас не находит. В меню F8 их видно только с
включённым Developer mode (страница Experimental, вперемешку с отладочными ключами), а в установщике их нет совсем.
Обход описан в `GUIDE.md`, но после публикации 0.1.0 это первое, обо что спотыкается новый игрок.

## What Changes

- В меню F8 появляется своя страница **On foot**, видимая всем без Developer mode. На ней пять строк:
  Native VR on foot, On-foot interface, On-foot aiming eye, On-foot head aim, On-foot aim dot.
- Эти пять строк уходят со страницы Experimental: одна настройка — одна строка.
- В окне настроек установщика появляется раздел **Native VR on foot (EDVRonFOOT)** с теми же пятью настройками.
  «Keep the settings I have changed» сохраняет их, как и остальные.
- Имена ключей в `edvr.ini` **не меняются** (`experimental.native_vr_on_foot*`): у игроков 0.1.0/0.1.1 файл
  остаётся рабочим, переносить ничего не нужно.
- `GUIDE.md` / `GUIDE.ru.md`: вместо обхода через Developer mode — «F8 → страница On foot» и раздел в установщике.
  Хвост «Настройки на ногах спрятаны» в `ROADMAP.md` закрывается.

## Capabilities

### New Capabilities

- `on-foot-settings`: где игрок находит и меняет настройки на ногах — страница On foot в меню F8 и раздел в окне
  настроек установщика.

### Modified Capabilities

- `on-foot-hud`: требование «Крепление листа» говорит, что ключ виден на странице Experimental; теперь — на
  странице On foot.

## Impact

- `edvr/edvr.ini` (шаблон): токен `menu on_foot` в строках `# ui:` пяти ключей.
- `edvr/tools/gen_settings_schema.py`: разрешить этот токен для `[experimental]`-ключей, такие строки — для всех
  (Fix-tier) на странице `on_foot`, не на Experimental; показать их в установщике. Самопроверки генератора.
- `edvr/src/d3d11/menu.cpp`: новая страница On foot в `buildPages()`.
- Установщик (`src/installer/`): проверить, что ключ `[experimental]` пишется в свою секцию и переживает обновление.
- Документы: `GUIDE.md`, `GUIDE.ru.md`, `edvr/README_EDVRONFOOT.md` (если упоминает Experimental), `ROADMAP.md`.
- Поведение на ногах и в корабле не меняется; меняется только, где видны настройки.

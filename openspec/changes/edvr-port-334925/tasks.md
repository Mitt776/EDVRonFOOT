# Tasks

## 1. Доказательство и правка

- [x] 1.1 Дверца, доказательство (design, решение 1): скрипт по образцу `prove_fp2.py` (только чтение exe) — все 11
  блоков `_FINGERPRINTS` с восстановленным отпечатком `334473`, байтовые точки, прямые вызовы, указатели таблиц,
  ячейка `LoadLibraryW`, `.text` span. Таблица «старый RVA → новый RVA, длина, новый FNV, доказан» — датированной
  записью «Перенос профиля на 334925» в `edvr/docs/openxr-oculus-selection-2026-09-14.md`. Проверка: 11 из 11
  «доказан»; иначе — стоп и вопрос Диме.
- [x] 1.2 Дверца, правка: `edvr/src/common/elite_oculus_profile.h`, `edvr/tools/elite_oculus.py` (константы,
  фикстура, самопроверка), `edvr/src/installer/probe.cpp` (`kQualifiedEliteSha256`), стенд
  `edvr/tools/elite_oculus_test`; `install_edvr.py` и `build_diff_targets.json` — если сверяют ревизию. Проверка:
  `py -3.12 edvr\tools\elite_oculus.py --self-test` ok; `py -3.12 edvr\tools\elite_oculus.py <exe игры>` принимает
  профиль `elite-odyssey-ed5a8f8c-libovr-r1`; стенд `--game <exe>` и `--sec-image <exe>` ok.
- [x] 1.3 Зонд HUD (design, решение 2): `edvr/src/d3d11/on_foot_hud_probe_core.h` — метка, 6 точек, три адреса
  данных, тексты строк и комментарии о билде; стенд `on_foot_hud_probe_test`. Каждый адрес данных — с двумя
  признаками, записать в `docs/research/native-vr-on-foot.md` разделом «Перенос на 334925». Проверка: grep по
  `edvr/src` — нет `0x6ACD513D` и `6891f70a` в коде (кроме истории в комментариях); стенд проходит.

## 2. Документы, сборка, установка

- [x] 2.1 `edvr/README_EDVRONFOOT.md` — поддерживаемая ревизия `334925`; `## Status` в
  `docs/research/native-vr-on-foot.md` и в `edvr/docs/openxr-oculus-selection-2026-09-14.md` — перенос и следующий шаг,
  не длиннее 60 строк. Спеки-дельты этой заявки сходятся с кодом.
- [ ] 2.2 Полная сборка (PATH с Python312, `TEMP`/`TMP` = `edvr\build\tmp`, `build.bat --jobs 8` по абсолютному
  пути): «all gates passed», `full-pass`; коммит `git commit -F`, тег `v0.19.1-edvronfoot.23`; `build.bat --dll-only`.
- [ ] 2.3 Установка при закрытой игре: `install_edvr.py --target <папка игры> --all --tag edvronfoot
  --keep-backups 1000 --dry-run`, затем без `--dry-run`, без `--allow-unqualified-game`. Проверка: `--verify-only` ok;
  живой `edvr.ini` не изменён.

## 3. Проверка

- [ ] 3.1 Запуск Димой: игра открывается из Steam, VR в корабле. По логу (`edvr_log.py --expect-build
  v0.19.1-edvronfoot.23` не 2): `oculus route: native=1 profile=1 ... installed=1`; зонд HUD — строки «installed» у
  всех точек, без «NOT installed». Затем — прогулка `.22` (задача 4.1 заявки `native-vr-on-foot-hud-layout`).

## Workflow follow-up

- Архивировать после 3.1 (`/opsx:archive edvr-port-334925`).

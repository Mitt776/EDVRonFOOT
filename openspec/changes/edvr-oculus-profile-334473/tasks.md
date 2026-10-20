# Tasks

## 1. Доказательство и профиль

- [x] 1.1 Довести `prove_fp.py` (скретчпад, только чтение exe) до вердикта по всем 11 блокам отпечатков: старый FNV
  восстановлен из новых байт с переписанными смещениями. Выписать в `edvr/docs/openxr-oculus-selection-2026-09-14.md`
  отдельной датированной записью таблицу «старый RVA → новый RVA, длина, новый FNV, доказан / чем отличается» и
  найденные сдвиги областей. Проверка: 11 из 11 «доказан». Если хотя бы один блок не доказан — стоп, разбор функции
  дизассемблером и вопрос Диме, в код не идём
- [x] 1.2 `edvr/src/common/elite_oculus_profile.h`: `kProfileId`, `kGameSha256`, `kLoadLibraryIatRva`,
  `kLoaderCallRva`, `kLoaderReturnRva`, stamp `0x6ACD513D`, SizeOfImage `0x640C000`, порог `.text` `0x4DB4000`,
  11 отпечатков, 9 байтовых точек, 5 прямых вызовов, 3 указателя таблиц — все значения 334473 из 1.1. Комментарии
  про ревизию — на 334473. Проверка: grep — в `edvr/src` нет `0x6A989634`, `0x4DB32E0`; `e6be8bbe` остаётся только в комментариях других частей EDVR, привязанных к 332841 (вне заявки), и в `probe.cpp` до 1.3
- [x] 1.3 `edvr/tools/elite_oculus.py`: те же значения (`GAME_SHA256`, `PROFILE_ID`, `_BYTE_CHECKS`,
  `_FINGERPRINTS`, вызовы и указатели в `_validate_image`, строгая проверка заголовка, `selector_rva`, `factory_rva`),
  фикстура `_fixture()` и самопроверка с `file_version="334473"`. `edvr/src/installer/probe.cpp`:
  `kQualifiedEliteSha256`. Проверка: `py -3.12 edvr\tools\elite_oculus.py --self-test` ok; `py -3.12
  edvr\tools\elite_oculus.py <exe игры>` печатает профиль `elite-odyssey-6891f70a-libovr-r1` с `load_library_iat_rva
  0x4db62e0`
- [x] 1.4 Стенд `edvr/tools/elite_oculus_test/elite_oculus_test.cpp`: синтетический образ (SizeOfImage, stamp,
  разделы, ячейка импорта) и списки адресов мутаций — на 334473. Проверка: `--self-test` и `--dry-run` ok; `--game
  <exe игры>` и `--sec-image <exe игры>` ok (exe не меняется: стенд читает файл и мапит копию)
- [x] 1.5 `edvr/tools/install_edvr.py`: текст предупреждения `--allow-unqualified-game` и комментарий над
  `ALLOW_UNQUALIFIED_GAME` — на этой ревизии EDVR не даст игре запуститься, пока профиль Oculus не перенесён, а
  `--suspend` возвращает игру; поправить самопроверку, если она сверяет текст. Проверить `build_diff_targets.json`:
  если сборка требует его эталон для текущего exe — обновить по правилам его инструмента, иначе не трогать.
  Проверка: `install_edvr.py --self-test` ok; `--verify-only` без ключа на папке игры проходит проверку exe

## 2. Документы, сборка, установка

- [x] 2.1 README fork-а (`edvr/README_EDVRONFOOT.md`): поддерживаемая ревизия — 334473, что делать после следующего
  обновления игры (`--suspend`, ждать переноса профиля). `docs/research/native-vr-on-foot.md`, `## Status` — «.9,
  дверца на 334473», не длиннее 60 строк. В `design.md` заявки `native-vr-on-foot-pattern-switch` к решению 7
  дописать поправку со ссылкой на эту заявку. Проверка: в README нет утверждения, что DLL ревизию не проверяет
- [x] 2.2 Полная сборка `cmd /c "D:\Project VSC\EDVRonFOOT\edvr\build_edvronfoot.bat"`, коммит (`git commit -F`),
  тег `v0.19.1-edvronfoot.9`, `build.bat --dll-only`. Проверка: «Native OpenXR build and all gates passed»,
  единственный `KNOWN FAILURE tolerated: weapon_motion`, `config contract ok`
- [x] 2.3 Установка при закрытой игре: `py -3.12 edvr\tools\install_edvr.py --target "C:\Steam\steamapps\common\Elite
  Dangerous\Products\elite-dangerous-odyssey-64" --all --tag edvronfoot --dry-run`, затем без `--dry-run`, без
  `--allow-unqualified-game`. Живой `edvr.ini` до и после — один хэш. Проверка: `--verify-only` ok;
  `edvronfoot_game.ps1 -Action Status` показывает `.9`

## 3. Проверка

- [x] 3.1 Запуск Димой: игра стартует из Steam, в шлеме VR в корабле. Проверка по логу (`edvr_log.py --expect-build
  HEAD`): версия `.9`; строка `oculus route: native=1 profile=1 ... installed=1`; если игра пыталась загрузить LibOVR —
  `rejected` ≥ 1, `exact` ≥ 1; в `edvr_breadcrumbs.txt` нет нового «process exit» сразу после старта. После этого —
  полёт варианта Б (задача 3.1 `native-vr-on-foot-pattern-switch` и разведка `native-vr-on-foot-ui-recon`) на `.9`
  (2026-10-20, `edvr_gfx_20261020_231928.log`: `.9` совпадает с HEAD; `installed=1`, `exact=2 rejected=2`; игра
  проработала 23 минуты, сбой был только при закрытии сессии — давний сбой выхода, не дверца)

## Workflow follow-up

- Архивировать после 3.1 (`/opsx:archive edvr-oculus-profile-334473`); главная спека `game-revision-gate` создаётся из
  этой дельты.
- В заявке `native-vr-on-foot-pattern-switch` задача 3.1 проверяется на `.9`, а не на `.8`: `.8` на 334473 не
  стартует.

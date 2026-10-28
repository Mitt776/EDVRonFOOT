# Tasks

## 1. Режим сессии в Config (фиксация при запуске и пересчёт старых значений)

- [x] 1.1 В `edvr/src/common/config.cpp`/`config.h` при первом успешном `parse()` запомнить «выбрано в файле» и «режим сессии»
  (`on`/`fsr` → `off`, без учёта регистра). При каждом следующем `parse()` класть под `fix.temporal_aa` режим сессии, а
  свежее значение из файла хранить как «выбрано в файле». Добавить метод для меню, который отдаёт «выбрано в файле».
  Проверка: `build.bat` (по абсолютному пути) собирается без ошибок.
- [x] 1.2 Строки лога, каждая не чаще одного раза на событие:
  - при запуске — «temporal_aa: session mode = <режим>»;
  - при `on`/`fsr` в файле — «<значение> is no longer supported, running as off»;
  - при новом значении в файле, отличном от режима сессии, — «temporal_aa: <значение> chosen, takes effect after restart».

  Проверка: тест из 1.3 видит эти строки.
- [x] 1.3 Дополнить `edvr/tools/config_test/config_test.cpp` случаями:
  - `fsr` и `on` читаются как `off`;
  - `dlaa` и `dlss` остаются как есть;
  - после перечитывания файла с другим значением `getString("fix.temporal_aa")` и `requestedTemporalMode()` отдают режим
    сессии, а метод для меню — новое значение;
  - прочие ключи из той же правки подхватываются.

  Проверка: стенд `config_test` зелёный в `build.bat`.
- [x] 1.4 Пройти места, где сейчас разбираются `on`/`fsr` для выбора (`menu.cpp` — подписи «TAA»/«FSR3»;
  `installer/settings_view.cpp` — скрытие пресета при `fsr`), и убрать ветки, ставшие мёртвыми. Код самих движков TAA и FSR
  (`temporal_pass`, `fsr3_engine`) и `temporal_mode.h` не трогать (design, решение 2). Ключ `intro_video` с его `fsr` не
  трогать. Проверка: `build.bat` зелёный, `installer_test` поправлен под новые варианты и проходит.

## 2. Меню F8, установщик и описание ключа

- [x] 2.1 В `edvr/edvr.ini` (строки ~317–376):
  - строку `# ui:` заменить на `# ui: Anti-aliasing | choices off, dlss=DLSS | menu performance` (без `live`);
  - переписать текст над ключом: только off/dlss (+ скрытое dlaa), on/fsr работают как off, режим действует со следующего
    запуска.

  Проверка: после `build.bat` в `build/gen/menu_schema.inc` и `settings_schema.inc` для `temporal_aa` стоит
  `off|dlss=DLSS`.
- [x] 2.2 В `edvr/src/d3d11/menu.cpp` строка Anti-aliasing показывает «выбрано в файле». Если оно отличается от режима
  сессии, добавлять пометку `after restart`, для профилей VR и flat одинаково. Проверка: `build.bat` зелёный; если у меню
  есть стенд с разбором строк (`menu_flat_rows`/`flat_sharpen_test`), он проходит.
- [x] 2.3 Записать в `docs/research/native-vr-on-foot.md` короткую запись под «Заход 5»: заявка `temporal-aa-dlss-only`,
  что убрано и почему, и обновить блок `## Status` документа. Проверка: `python -I edvr\tools\check_status_blocks.py`
  сообщает, что все блоки в пределах 60 строк.

## 3. Установка и ручная проверка в шлеме (Дима)

- [ ] 3.1 Поставить сборку: `python edvr\tools\install_edvr.py --target steam --dry-run`, затем без `--dry-run`. Проверить:
  `python edvr\tools\install_edvr.py --target steam --verify-only` и
  `python edvr\tools\edvr_log.py --target steam --expect-build HEAD --version` после запуска игры. Живой `edvr.ini` не
  трогается: `temporal_aa = dlss` остаётся.
- [ ] 3.2 Дима: запустить игру с `temporal_aa = dlss`, появиться на станции. В меню F8 убедиться, что у Anti-aliasing
  только «Off» и «DLSS», выбрать «Off» и нажать PAUSE (метка). Ожидаемо: картинка не меняется, слайд-шоу нет, у строки
  пометка `after restart`. Проверка по логу (`edvr_log.py`): строки «session mode = dlss» и «off chosen, takes effect after
  restart», после метки нет SLOW REGIME и FREEZE от смены режима.
- [ ] 3.3 Дима: снова выбрать «DLSS» — пометка пропадает. Затем «Off», выйти из игры и запустить снова. Ожидаемо: игра
  идёт без сглаживания, в логе «session mode = off».
- [ ] 3.4 Дима: вернуть `dlss` через меню, перезапустить игру, сесть в корабль, выйти на ноги на станции (переход корабль →
  ноги с DLSS) и нажать PAUSE. Проверка по логу: после метки нет слайд-шоу (кадры не по 0.3–4 с, нет SLOW REGIME
  «held by the vendor runtime's xrEndFrame»).
- [ ] 3.5 Проверка старого значения: на копии ini (через `install_edvr.py --ini` с бэкапом, не руками по живому файлу)
  поставить `temporal_aa = fsr`, запустить игру. Ожидаемо: сглаживания нет, в логе строка «fsr is no longer supported,
  running as off», файл по-прежнему с `fsr`. Затем вернуть ini Димы (`dlss`) тем же инструментом.

## Workflow follow-up

- Закоммитить по правилам `edvr/AGENTS.md` (сообщение через файл и `git commit -F`).
- Архивировать заявку (`/opsx:archive`) после зелёной ручной проверки. Спеки `temporal-aa` и дельта `edvr-fork` сольются
  в основные.

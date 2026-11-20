# Tasks

## 1. Ветка и PR

- [x] 1.1 Ветка `change/on-foot-marker-pose` от `main` (создана 20.11 на `821ded8`); коммит заявки `git commit -F`, почта
  noreply (`git log -1 --format="%ae %ce"`); push; PR в `main` по шаблону: ссылка на заявку, план полёта из 4.1.

## 2. Ключ и канал кадра (d3d11-сторона)

- [x] 2.1 `edvr.ini`: временный ключ `experimental.native_vr_on_foot_marker_pose = next` с описанием и строкой
  `# ui: On-foot marker pose | live | menu` (страница Experimental, не On foot); проверка: стенд меню/ini в полной сборке
  проходит, на странице On foot по-прежнему шесть строк. *Сделано:* метка `| menu` у ключа `[experimental]` — ошибка
  сборки (`gen_settings_schema.py`, «stray token»); без неё ключ и так строка Experimental в режиме разработчика
  (`# ui: On-foot marker pose | choices next, frame | live`).
- [x] 2.2 `common/native_frame.h`: поле `markerPoseFrame` в конец `EdvrNativeFrameOutput`, версия 9, размер/offset как у
  прошлых версий; `native_frame_client.h` — старая сторона без поля = 0. Проверка: стенд `openxr_native_test` с версиями 8
  и 9.
- [x] 2.3 `d3d11/native_frame.cpp`: `markerPoseFrame = (ключ == frame) && journalOnFootKnown() && journalOnFoot()`;
  непонятное значение — `next` и одна строка `'…' is not next or frame: read as next`. Проверка: сборка, строка видна в
  стенде или в логе при ручной правке ini.

## 3. Ответ по шагу кадров (рантайм)

- [x] 3.1 `head_pose_time.h`: чистая функция шага (медиана 16 разностей, пол 1 такт, потолок `min(4 такта, 50 мс)`) и
  `answerTime` с шагом вместо такта и допуском свежести = шаг (design, решения 1 и 4). Проверка: `tools\openxr_pose_test` —
  ровные 1 и 2 такта, смесь 1/2, выброс, пустая история, потолок, переполнение; режим `next` даёт прежние ответы.
- [x] 3.2 `native_runtime_host.h`: учёт разностей `predictedDisplayTime` после `admitFrame`; `poseTargetFor` и
  `gameplayTime` WaitGetPoses берут шаг при флаге `markerPoseFrame` (для WaitGetPoses — флаг прошлого кадра). Сброс истории
  при смене сессии/начала координат/геометрии. Проверка: `openxr_native_test` — при флаге 0 ответы побайтно как раньше, при
  флаге 1 и шаге два такта ответ на `D + 2P`.
- [x] 3.3 Строки: `marker pose: next|frame (<причина>)` на смене; 30-с строка `marker pose: frame, step p50 … max …,
  answered N` (0 — отличимо); `pose_gap.h`: поле ` mode next|frame` в конце строки, окно закрывается на смене режима.
  Проверка: стенд ловит строку смены и закрытие окна; трассировка «что будет в логе, если код не сработал» = `answered 0`.
- [x] 3.4 `tools/edvr_log.py --tally pose`: разбор поля режима, таблица по режимам; `--self-test` с образцами строк с полем
  и без (старые логи читаются). Проверка: `python tools\edvr_log.py --self-test`.

## 4. Доки, сборка, установка

- [x] 4.1 `docs/research/native-vr-on-foot.md`: раздел «Сползание подсказок» (наблюдение Димы 20.11, лог прогулки
  `edvr_openxr_20261120_185214_985_30920.log`, гипотеза, зонд, план полёта) и строка в `## Status` (временный ключ);
  ROADMAP — пункт «Стабилизация подсказок»: зонд в работе. Проверка: `check_status_blocks.py` проходит.
- [x] 4.2 Полная сборка `build.bat --jobs 8` по полному пути → «all gates passed»; коммит; `build.bat --dll-only`;
  `install_edvr.py --target steam --dry-run`, установка, `--verify-only`, хеш `edvr.ini` не изменился без `--ini`
  (ключа нет в ini → действует `next`; строку Дима увидит в F8 после установки ini или правки — решить при установке). *Сделано 20.11:* сборка `3de1b7b`, `verify` ok, хеш `edvr.ini` AAE0CFF8 не менялся; строка в F8 есть и без строки в ini (схема меню вшита).

## 5. Полёт и разбор

- [x] 5.1 Полёт Димы (proposal, «Как проверить руками»): станция, NPC с подписью, `next` ~1 мин → `frame` ~1 мин →
  можно ещё раз; кивки и повороты головы, смотреть на подпись. По желанию 2 мин на планете в `frame` — края картинки.
- [x] 5.2 Разбор: `edvr_log.py --expect-build HEAD`; строки `marker pose:` со временем смен; 30-с строки `answered` > 0 при
  `frame`; `--tally pose` по режимам (`angle-to-drawn` в `frame` ниже — подтверждение, что режим действует, не
  доказательство); слова Димы — решающие. Итог и `ruled out` (если не помогло) — в доклад.

## 6. Итог зонда

- [x] 6.1 Спросить Диму с цитатой ключа `experimental.native_vr_on_foot_marker_pose` и его поведения: удалить ключ и
  оставить победителя поведением. Сделать по ответу (код, ini, F8, спека дельтой этой заявки — обновить требование до
  поведения без ключа), стенды, полная сборка, установка, короткий контрольный полёт.

## Workflow follow-up

- Архив заявки в этой же ветке после полёта и «да» Димы (`/opsx:archive`), спека синхронизируется.
- Слияние PR локально (`git merge --no-ff`), проверка почты noreply, push, удалить ветку.

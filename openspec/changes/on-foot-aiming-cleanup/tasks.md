# Tasks

## 1. Прицел головой без зонда (код и стенд)

- [x] 1.1 `git mv` `edvr\src\d3d11\head_aim_probe.cpp/.h` → `head_aim.cpp/.h`; функции `headAimInstall`,
  `headAimFrameBoundary`; вызовы в `device_hook.cpp` и список исходников в `build.bat` поправлены (design, решение 1).
  Проверка: `Grep head_aim_probe` в `edvr\src` и `build.bat` — только комментарии об истории канала (`native_frame.h`,
  рантайм).
- [x] 1.2 Общие части из `head_aim_probe_core.h` (проверка байтов, строки `hooked`/`inert`, ворота «на ногах»,
  перепись HMC, опознание игрока и его строка) перенести в `head_aim_core.h` (`ha::`), префикс строк `head aim:`;
  `head_aim_probe_core.h` удалить. Из `head_aim.cpp` убрать сдвиги зонда, качание, компенсацию, строку `watch`,
  сводку зонда и чтение двух ключей зонда; в защёлке `Turn.probe` всегда 0. Проверка: `Grep "head aim probe"` и
  `hap::` в `edvr\src` — пусто.
- [x] 1.3 `head_aim_core.h`: убрать `Key::Raw`, `PitchComp`, `Sync`, `StickComp` и их разборы, разбор
  `pitch_offset`; поворот позы по наклону — `added`, такт — углы камеры, поправка — по наклону головы, константа
  `kPitchOffsetDeg = 10`; разбор ключа фичи по решению 4 design (`off` / иначе `on`, неизвестное — строка
  `reads on`); строка ключа `head aim: key=on|off`; строка такта без режима. Проверка: в `head_aim.cpp` читается
  только `experimental.native_vr_on_foot_head_aim`.
- [x] 1.4 Стенд: удалить `edvr\tools\head_aim_probe_test\` и гейт `:rig_head_aim_probe_test` в `build.bat`; в
  `edvr\tools\head_aim_test\` перенести проверки опознания, ворот и строк хуков, убрать проверки удалённых
  вариантов, добавить проверки разбора ключа (нет ключа, пусто, `on`, `off`, `raw`, мусор). Проверка: `build.bat`
  остаётся CRLF (`[IO.File]::ReadAllBytes` — нет одиночных `0x0A`), стенд печатает `PASS: head_aim_test, N checks`.
- [x] 1.5 `edvr\edvr.ini`: удалить блоки `native_vr_on_foot_head_aim_probe`, `..._probe_compensate`,
  `..._pitch_comp`, `..._sync`, `..._stick_comp`, `..._pitch_offset`; блок `native_vr_on_foot_head_aim` переписать
  простыми словами (постоянный, `on` по умолчанию, `off` — ствол по телу, без слова «Temporary»), строка
  `# ui: On-foot head aim | choices on, off | live`, значение `on`. Проверка: `Grep head_aim` в `edvr.ini` — один
  ключ; генератор F8 (`python tools\gen_settings_schema.py --self-test` из `edvr\`) без ошибок.

## 2. Ширина HUD без ключа

- [x] 2.1 `on_foot_hud.cpp` / `on_foot_hud_core.h`: ширина — константа 85 (`kWidthDefault`), чтения
  `native_vr_on_foot_hud_size` и разбора ширины нет; строка ключей HUD называет только крепление. Проверки разбора
  ширины на стенде HUD убрать. Проверка: `Grep hud_size` в `edvr\src` и `edvr\tools` — пусто; стенд HUD `PASS`.
- [x] 2.2 `edvr\edvr.ini`: удалить блок `native_vr_on_foot_hud_size`. Проверка: `Grep hud_size edvr\edvr.ini` — пусто.

## 3. Ведущий глаз — постоянная настройка

- [x] 3.1 `edvr\edvr.ini`: из описания `native_vr_on_foot_aim_eye` убрать «Temporary, while on-foot aiming is
  tuned in flight»; в `on_foot_aim_eye.h/.cpp`, `on_foot_aim_eye_core.h` и комментарии гейта в `build.bat` — слово
  «временный»/TEMPORARY. Поведение не меняется. Проверка: `Grep -i "temporar" ` по этим файлам — пусто;
  `on_foot_aim_eye_test` `PASS`.

## 4. Сборка `.46`, документы, установка

- [x] 4.1 `D:\Project VSC\EDVRonFOOT\edvr\build.bat --jobs 8` по абсолютному пути: «Native OpenXR build and all
  gates passed» (`weapon_motion` — известный терпимый сбой); в хвосте `PASS: head_aim_test`, нет
  `head_aim_probe_test`. Перечитать изменённые места на порядок объявлений и одинаковые строки лога.
- [x] 4.2 `docs\research\native-vr-on-foot.md`: в `## Status` — строка про уборку (`.46`), список ключей: прицел
  головой постоянный `on`/`off` (по умолчанию `on`), глаз постоянный, остальные — удалены в `.46` (решение Димы
  02.11), `hud_size` удалён (решение 02.11 вместо 26.10); в конце раздел «Уборка ключей прицела (`.46`)» с тем,
  что проверить в полёте. `ROADMAP.md` пункт 4 — ГОТОВО. Проверка: `python tools\check_status_blocks.py` (из `edvr\`)
  — все блоки ≤ 60 строк.
- [x] 4.3 Коммит (сообщение в файл, `git commit -F`), тег `v0.19.1-edvronfoot.46`, `build.bat --dll-only`,
  `python tools\install_edvr.py --target steam --dry-run`, затем без `--dry-run`, затем `--verify-only` — «vr
  payload and profile verified».
- [x] 4.4 Живой `edvr.ini` игры: прочитать, Edit-ом убрать строки семи удалённых ключей; `native_vr_on_foot_head_aim`
  — `on` (если было `raw`, заменить). Проверка: diff до/после, `Grep` по живому ini — только `head_aim = on` и
  `aim_eye`.
- [ ] 4.5 После запуска игры (Дима): `python tools\edvr_log.py --target steam --expect-build HEAD --grep "head aim"`
  — строки `head aim: hooked`, `head aim: key=on`, на ногах `head aim: player identified`; строк `head aim probe:`
  и `not understood` нет.

## 5. Полёт `.46` (Дима)

Что сделать руками в шлеме, по порядку. Ничего в F8 не включать заранее — проверяем умолчания.

- [ ] 5.1 Выйти из корабля на станции: ствол и точка прицела сразу идут за головой, мир стоит при поворотах и
  наклонах головы, быстрый поворот — без рывков. Проверка: Дима «как в `.40`».
- [ ] 5.2 Стрельба от бедра и с LT: точка в центре взгляда, пули туда, куда смотришь; с LT мушка перед правым глазом,
  при повороте головы с LT мушка не уезжает. Проверка: Дима «попадает».
- [ ] 5.3 Меню лифта/двери с повёрнутой головой: взгляд не уходит в пол, меню не мигает; после закрытия прицел снова
  за головой. Около NPC — они не крутятся за головой. Проверка: Дима «ок».
- [ ] 5.4 F8: у прицела головой один пункт `on`/`off`, пунктов зонда, знака, такта, стика, поправки и ширины HUD нет;
  `off` — ствол по телу, `on` — за полсекунды снова за головой. Проверка: Дима «ок».
- [ ] 5.5 HUD, лифт, ответы NPC, торговец — того же размера и на тех же местах, что в `.45`. Проверка: Дима «как было».
- [ ] 5.6 После полёта: `edvr_log.py --expect-build HEAD --grep "head aim"` — сводки с ненулевыми кадрами прицела,
  строк `head aim: inert` и `player not identified` на станции нет; записать в документ дуги раздел «Полёт `.46`», обновить `## Status`.

## Workflow follow-up

- После полёта — `/opsx:archive` (спека `on-foot-head-aim-probe` уходит, `on-foot-aiming` и `on-foot-hud`
  обновляются).
- Обновить память `on-foot-ui-and-aiming.md`: дуга прицела закрыта уборкой.

# Design

## Context

Зачем — см. `proposal.md`. Что должно получиться — дельты в `specs/`.

Как устроено сейчас (`.25`):
- `native_vr_probe.cpp` (1024 строки) и `native_vr_probe_core.h` (1001) — это два дела в одном модуле:
  - **зонд и удержание:** 10 хуков сборки 332841 (проверка по PE-штампу), кольцо событий, ограничитель строк, сторож
    записи на отладочных регистрах, удержание стерео в источнике режима. На игре 334925 не ставится ни один хук (лог
    `.25`: «installed on 0 of 10 points»);
  - **два переключателя, найденные по коду игры:** байт кинотеатра (`nativeVrCinemaSwitchFind/Step`, `nvp::findCinemaSwitch`,
    `cinemaSwitchStep`, `formatFind`, `formatSwitch`) и число HUD (`nativeVrHudSwitchFind/Step`, `findHudSwitch`,
    `hudSwitchStep`, `formatHudFind`, `formatHudSwitch`). Им нужны только помощники чтения и записи памяти игры под SEH
    (`sehFindSwitch`, `sehReadByte`, `sehSwapByte`, `sehReadInt`, `sehSwapInt`) и `say`/`logSink`. В ядре это
    строки ~532–884; в стенде `native_vr_probe_test` — части G и H.
- `nativeVrProbeFrameBoundary` читает три ключа (`native_vr_probe`, `native_vr_on_foot`, `native_vr_on_foot_game_hud`),
  при первом включении ставит хуки и ищет оба переключателя, каждый кадр шагает их, сливает кольцо в лог.
- `on_foot_hud_probe.*`, `on_foot_hud_probe_core.h` (4104 строки), `on_foot_hud_layout.*` и стенд (3849 строк) —
  отдельный зонд. Входы в него: `device_hook.cpp` (include, тик кадра `tkOnFootHudProbe`, сохранение кода вершинных
  шейдеров при `onFootHudProbeKeepsCode()`), `vscreen.cpp` (include, вызов `onFootHudProbeNoteDraw` в пути рисования,
  сама функция `onFootHudProbeNoteDraw`). Метки `.18` (`on_foot_markers*`) от зонда не зависят — так сказано в их
  заголовке, includes это подтверждают. Лист HUD (`on_foot_hud*`) тоже не зависит.
- `debug_register_core.h` нужен и другим: `flat_camera_producer_probe.cpp`, стенд `transition_flash_prevent_test`.
  Он остаётся.
- Неизвестные ключи в живом `edvr.ini` EDVR молча пропускает (как `on_foot_hud_panel` на `.25`).

## Goals / Non-Goals

**Goals:**
- На игре 334925 поведение `.25` без изменений: те же две строки поиска, те же 1 → 0 и -1 → 1, те же листы.
- Переключатели переносятся без изменения логики и с теми же проверками стенда.

**Non-Goals:**
- Ключи `native_vr_on_foot_hud_size`, `native_vr_on_foot_hud`, `native_vr_on_foot` — без изменений.
- Метки `.18`, лист HUD и меню — не трогаются.
- `debug_register_core.h` не удаляется и не меняется, кроме комментариев.

## Decisions

### 1. Новый модуль `native_vr_on_foot` вместо урезанного `native_vr_probe`

Переключатели переезжают в `native_vr_on_foot.h/.cpp` и `native_vr_on_foot_core.h`; файлы `native_vr_probe*` удаляются.
Имена функций переключателей (`nativeVrCinemaSwitchFind/Step`, `nativeVrHudSwitchFind/Step`) уже без слова «probe» и
остаются. Граница кадра — `nativeVrOnFootFrameBoundary`, тик в `device_hook.cpp` — `tkNativeVrOnFoot`
("native_vr_on_foot"). Пространство имён ядра `nvp` → `nvf`.

Почему: в модуле с именем «зонд» жил бы рабочий нативный VR — следующий читатель снова примет его за временный код.

Отброшено: «оставить файлы, вырезать середину» — меньше правок, но имя врёт.

### 2. Граница кадра без хуков

`nativeVrOnFootFrameBoundary`: читает только `native_vr_on_foot` (нет — значит 1); при первом включении ищет оба
переключателя (как сейчас, `tried`); каждый кадр шагает переключатель кинотеатра с ключом и число HUD с тем же ключом
(было `onFootKey && gameHudKey`). Всё про хуки, кольцо, удержание, сторож и журнал удержания уходит.

### 3. Строки лога

- Строка ключа (`formatOnFootKey`): те же «on»/«off», без слов об удержании. Вкл: `native-vr-on-foot: on: on foot the
  game keeps headset stereo (its cinema-on-foot switch off) (experimental.native_vr_on_foot = 1)`. Выкл: `off: the
  cinema-on-foot switch goes back to the game; the next disembark is the game's cinema again`.
- Строка ключа HUD игры (`formatGameHudKey`) уходит: о HUD игры говорят строки переключателя (`game HUD switch found …`,
  `game HUD on: … -1 (auto) -> 1 (on)`), они не меняются.
- Уходят строки `native-vr-probe: …`, «the hold (build 332841 only) is not installed», «cannot work: the stereo mode
  source is not hooked…». Вместо последней: если переключатель кинотеатра не найден, его собственная строка
  `head camera cannot work…` (она уже есть в `nativeVrCinemaSwitchStep`).
- Остальные строки `native-vr-on-foot:` — дословно как на `.25`.

### 4. Зонд HUD — целиком

Удаляются `on_foot_hud_probe.{h,cpp}`, `on_foot_hud_probe_core.h`, `on_foot_hud_layout.{h,cpp}`, стенд. В
`vscreen.cpp` уходят include, вызов в пути рисования (`if (onFootHudProbeWantsDraws()) …`) и функция
`onFootHudProbeNoteDraw`; `pureDrawReissue` остаётся (им пользуются другие), из её объявления-вперёд уходит слово про
зонд. В `device_hook.cpp` — include, тик и сохранение кода шейдеров. Путь рисования от этого не меняется: вызов и так
не шёл при ключе 0.

### 5. Стенд `native_vr_on_foot_test`

Части G и H старого стенда (поиск обоих узоров на синтетическом образе, шаги переключателей, строки) переезжают с их
помощниками (`check`, `sink`, `anyLine`, заглушки журнала). Число проверок этих частей до и после переноса сверяется:
перенос без потерь. Части A–F (зонд, кольцо, хуки, сторож) уходят. `build.bat`: раздел стенда переименован, линкует
`native_vr_on_foot.cpp`; разделы двух старых стендов и три исходника в списке DLL уходят.

### 6. Ключи

Шаблон `edvr/edvr.ini`: `native_vr_probe`, `on_foot_hud_probe`, `native_vr_on_foot_game_hud` уходят вместе с
описаниями (контракт 132 → 129). Живой `edvr.ini` в папке игры: три строки убираются правкой Edit с диффом до и после
и копией файла в scratchpad (правило проекта). Их удаление ничего не меняет — EDVR их больше не читает; это чтобы
F8 и файл не показывали мёртвые ключи.

## Risks / Trade-offs

- [Перенос сломает поиск переключателя] → Код переносится дословно; стенд с теми же проверками частей G и H;
  прогулка сверяет две строки поиска с `.25` (`+0x5E85B8A`, `+0x5E85B94`) и записи 1 → 0, -1 → 1.
- [Кто-то ещё зовёт код зонда] → `Grep` по `src` и `tools`: только `device_hook.cpp`, `vscreen.cpp` и сами модули;
  сборка без них упадёт на любой забытой ссылке.
- [Старая строка `native_vr_on_foot_game_hud = 0` у кого-то в `edvr.ini`] → HUD игры включится. Это и есть решение
  (сценарий «Старая строка ключа»); выключить — `native_vr_on_foot = 0`. У Димы стоит 1.
- [Игра 332841 без удержания] → Её в Steam уже не получить; если байт кинотеатра на ней найден по коду, нативный VR
  работает тем же переключателем.
- [Тики кадра переименованы] → Имя тика видно только в строках замеров; инструменты лога его не разбирают (проверено
  `Grep` по `tools`).

## Migration Plan

- Сборка `.26`, установка `install_edvr.py --all --tag edvronfoot`. Живой `edvr.ini`: три строки удаляются отдельным
  шагом с диффом.
- Откат: переустановить `.25` тем же скриптом (бэкапы хранятся); строки ключей в `edvr.ini` вернуть не нужно — без них
  `.25` берёт значения по умолчанию (зонды 0, HUD игры 1), то есть то же поведение.

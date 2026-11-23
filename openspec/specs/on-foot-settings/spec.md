# on-foot-settings Specification

## Purpose

Где игрок находит и меняет настройки EDVRonFOOT на ногах: своя страница On foot в меню F8 для всех и раздел в окне
настроек установщика, без режима разработчика.

## Requirements

### Requirement: Страница On foot в меню F8
Меню F8 SHALL иметь страницу **On foot**, видимую при выключенном Developer mode. На ней SHALL быть ровно десять строк,
в этом порядке: Native VR on foot, On-foot interface, On-foot aiming eye, On-foot head aim, On-foot aim dot, On-foot
scope zoom, On-foot height, Desktop eye view, Desktop eye fit, Desktop eye steady. Каждая строка SHALL менять свой ключ
`experimental.native_vr_on_foot*` или `experimental.native_vr_desktop_view*` на лету, как сейчас на странице
Experimental.

#### Scenario: Без режима разработчика
- **WHEN** игрок с выключенным Developer mode открывает F8 и листает страницы Tab
- **THEN** среди страниц есть On foot, на ней десять строк настроек; страниц Advanced, Experimental и Instruments нет

#### Scenario: Изменение на лету
- **WHEN** игрок на ногах на странице On foot ставит On-foot aim dot в `show`
- **THEN** точка прицела появляется без перезапуска игры, а в `edvr.ini` записано `native_vr_on_foot_aim_dot = show`
  в секции `[experimental]`

#### Scenario: Зум в прицеле на лету
- **WHEN** игрок на ногах на странице On foot ставит On-foot scope zoom в `off`
- **THEN** при следующем прицеливании из Executioner линзы нет, а в `edvr.ini` записано
  `native_vr_on_foot_scope_zoom = off` в секции `[experimental]`

#### Scenario: Вид из глаза на лету
- **WHEN** игрок на странице On foot ставит Desktop eye view в `right`
- **THEN** окно игры на мониторе показывает правый глаз, а в `edvr.ini` записано `native_vr_desktop_view = right` в
  секции `[experimental]`

#### Scenario: Стабилизация в F8
- **WHEN** игрок на странице On foot ставит Desktop eye steady в `off`
- **THEN** в `edvr.ini` записано `native_vr_desktop_view_steady = off` в секции `[experimental]`

#### Scenario: Рост в F8
- **WHEN** игрок на ногах на странице On foot стрелками ставит On-foot height на `+5`
- **THEN** камера на ногах поднимается на 5 см без перезапуска, а в `edvr.ini` записано `native_vr_on_foot_height = 5`
  в секции `[experimental]`; стрелки идут шагом 1 см и не выходят за −10 и +20

### Requirement: Одна настройка — одна строка
Десять настроек страницы On foot SHALL NOT показываться на странице Experimental: при включённом Developer mode они
остаются только на странице On foot. Остальные ключи `[experimental]` SHALL остаться на Experimental, как были.

#### Scenario: С режимом разработчика
- **WHEN** игрок включает Developer mode и открывает страницу Experimental
- **THEN** строк Native VR on foot, On-foot interface, On-foot aiming eye, On-foot head aim, On-foot aim dot, On-foot
  scope zoom, On-foot height, Desktop eye view, Desktop eye fit и Desktop eye steady на ней нет; на странице On foot они есть

### Requirement: Раздел в установщике
Окно настроек установщика SHALL показывать те же десять настроек под заголовком «Native VR on foot (EDVRonFOOT)», с
вариантами и описанием из `edvr.ini`. Значение SHALL записываться в секцию `[experimental]` файла `edvr.ini` и
сохраняться при обновлении с включённым «Keep the settings I have changed».

#### Scenario: Смена в установщике
- **WHEN** игрок в установщике ставит On-foot interface в `visor` и нажимает Install
- **THEN** в `edvr.ini` в игре `[experimental]` содержит `native_vr_on_foot_hud = visor`, и в игре лист держится
  перед глазами

#### Scenario: Обновление сохраняет выбор
- **WHEN** у игрока в `edvr.ini` стоит `native_vr_on_foot_aim_eye = left`, и он ставит новую версию с «Keep the
  settings I have changed»
- **THEN** после установки значение по-прежнему `left`, и установщик показывает его как изменённое игроком

### Requirement: Имена ключей не меняются
Ключи SHALL остаться `native_vr_on_foot`, `native_vr_on_foot_hud`, `native_vr_on_foot_aim_eye`,
`native_vr_on_foot_head_aim`, `native_vr_on_foot_aim_dot`, `native_vr_on_foot_scope_zoom`, `native_vr_desktop_view` и
`native_vr_desktop_view_fit` в секции `[experimental]`; к ним добавляются `native_vr_desktop_view_steady` и
`native_vr_on_foot_height`. Файл
`edvr.ini` от 0.1.0 и 0.1.1 SHALL читаться без переноса.

#### Scenario: Старый файл настроек
- **WHEN** игрок с `edvr.ini` от 0.1.1, где `native_vr_on_foot_head_aim = off`, ставит новую версию
- **THEN** прицел головой выключен, и страница On foot показывает `off`

#### Scenario: Файл без нового ключа
- **WHEN** игрок с `edvr.ini` от 0.1.6 без `native_vr_desktop_view` ставит новую версию
- **THEN** вид из глаза выключен, страница On foot показывает Desktop eye view как `off`

#### Scenario: Файл без ключа стабилизации
- **WHEN** у игрока `edvr.ini` без `native_vr_desktop_view_steady`, и он включает вид из глаза
- **THEN** стабилизация включена, страница On foot показывает Desktop eye steady как `on`

#### Scenario: Файл без ключа роста
- **WHEN** у игрока `edvr.ini` от 0.1.7 без `native_vr_on_foot_height`, и он ставит новую версию
- **THEN** высота камеры на ногах как раньше, страница On foot показывает On-foot height как `0`
